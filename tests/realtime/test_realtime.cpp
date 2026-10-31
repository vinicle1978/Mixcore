// Realtime-safety tests. This executable replaces global operator new/delete
// (support/AllocationTracker.cpp) so any heap activity on the audio thread
// inside process(), setSettings() or the calibrator's audio-side code fails.

#include "../support/AllocationTracker.h"
#include "../support/StageHarness.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <limits>
#include <chrono>
#include <thread>

using namespace mixcore;
using namespace mixcore::gain;
using namespace mixcore::test;

TEST_CASE("The audio path never allocates", "[realtime][allocation]")
{
    const double sr = 48000.0;
    SmartGainSettings s;
    auto stage = makeStage(sr, s, 2);
    // Exercise every code path: activity, riding, calibration, guard toggling,
    // bypass toggling, non-finite input, telemetry reads.
    auto mono = compose(sr, { { Segment::Silence, 0.5 }, { Segment::Vocal, 4.0, -26.0 }, { Segment::Silence, 0.5 },
                              { Segment::Vocal, 4.0, -8.0 } },
                        -60.0);
    mono[1000] = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> l = mono, r = mono;
    stage->calibrator().requestStart(1.5);

    SmartGainTelemetry t;
    std::size_t allocations = 0, deallocations = 0;
    int block = 0;
    for (size_t pos = 0; pos + 256 <= l.size(); pos += 256, ++block)
    {
        s.peakGuardEnabled = (block / 200) % 2 == 1;
        s.bypass = (block / 333) % 3 == 2;
        s.staticGainDb = static_cast<float>((block % 50) - 25) * 0.1f;
        float* ptrs[2] = { l.data() + pos, r.data() + pos };

        ScopedRealtimeCheck check;
        stage->setSettings(s);
        stage->process(ptrs, 2, 256);
        stage->readTelemetry(t);
        allocations += check.counts().allocations;
        deallocations += check.counts().deallocations;
    }
    CHECK(allocations == 0);
    CHECK(deallocations == 0);
    CHECK(stage->calibrator().state() == CalibrationState::Ready); // calibration ran on the audio thread
}

TEST_CASE("Telemetry and calibration control are safe across threads", "[realtime][threads]")
{
    const double sr = 48000.0;
    auto stage = makeStage(sr, SmartGainSettings {}, 2);
    auto mono = compose(sr, { { Segment::Vocal, 6.0, -24.0 }, { Segment::Silence, 0.5 }, { Segment::Vocal, 6.0, -15.0 } },
                        -65.0);

    std::atomic<bool> done { false };
    std::atomic<std::uint64_t> reads { 0 };
    std::atomic<bool> bad { false };

    std::thread ui([&] {
        std::uint64_t lastSeq = 0;
        int i = 0;
        while (!done.load())
        {
            SmartGainTelemetry t;
            if (stage->readTelemetry(t))
            {
                if (t.sequence < lastSeq || !std::isfinite(t.riderGainDb) || !std::isfinite(t.outputRmsDb))
                    bad = true;
                lastSeq = t.sequence;
                ++reads;
            }
            if (++i % 5000 == 0)
                stage->calibrator().requestStart(1.0);
            if (i % 7919 == 0)
                stage->calibrator().cancel();
            if (stage->calibrator().state() == CalibrationState::Ready)
                (void) stage->calibrator().computeResult(-18.0f, -3.0f);
        }
    });

    std::vector<float> l(256), r(256);
    std::vector<double> blockMs;
    blockMs.reserve(4 * mono.size() / 256 + 1);
    for (int pass = 0; pass < 4; ++pass)
        for (size_t pos = 0; pos + 256 <= mono.size(); pos += 256)
        {
            std::copy_n(mono.begin() + static_cast<long>(pos), 256, l.begin());
            std::copy_n(mono.begin() + static_cast<long>(pos), 256, r.begin());
            float* ptrs[2] = { l.data(), r.data() };
            const auto t0 = std::chrono::steady_clock::now();
            stage->process(ptrs, 2, 256);
            blockMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
    done = true;
    ui.join();

    CHECK_FALSE(bad.load());
    CHECK(reads.load() > 0);
    // The test thread is not realtime-priority, so a single OS preemption can
    // exceed any budget. Judge the processing cost by the 99.9th percentile
    // and report the worst case for information.
    std::sort(blockMs.begin(), blockMs.end());
    const double p999 = blockMs[static_cast<size_t>(0.999 * static_cast<double>(blockMs.size() - 1))];
    const double worstBlockMs = blockMs.back();
    const double budgetMs = 256.0 / sr * 1000.0;
    INFO("p99.9 block " << p999 << " ms, worst " << worstBlockMs << " ms, budget " << budgetMs << " ms");
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
    WARN("Deadline check skipped: sanitizer instrumentation distorts timing.");
#else
    CHECK(p999 < 0.25 * budgetMs); // comfortably inside the deadline with a busy reader thread
#endif
}

#include "mixcore/drum/StarterKits.h"
#include "mixcore/strip/ChannelPresets.h"

TEST_CASE("The full channel strip (Smart Gain + De-bleed + Gate) never allocates", "[realtime][allocation][strip]")
{
    using namespace mixcore::strip;
    const double sr = 48000.0;
    ChannelStrip strip;
    strip.prepare(sr, 256, 2);
    auto s = findChannelPreset("Snare Drum")->settings;
    strip.setSettings(s);
    strip.reset();
    strip.replacer().kits().publish(mixcore::drum::makeStarterKit("starter:snare")); // message thread, before the loop
    strip.gate().learner().requestStart(2.0);
    strip.gain().calibrator().requestStart(1.0);

    auto mono = compose(sr, { { Segment::Vocal, 3.0, -20.0 }, { Segment::Silence, 1.0 }, { Segment::Vocal, 3.0, -12.0 } }, -60.0);
    std::vector<float> l = mono, r = mono;
    mixcore::gate::GateTelemetry gt;
    mixcore::drum::DeBleedTelemetry dt;
    mixcore::drum::ReplaceTelemetry rt;
    mixcore::comp::CompTelemetry ct;
    SmartGainTelemetry st;
    std::size_t allocations = 0, frees = 0;
    int block = 0;
    for (size_t pos = 0; pos + 256 <= l.size(); pos += 256, ++block)
    {
        s.gate.lookaheadMs = (block / 150) % 2 ? 0.0f : 1.5f; // latency changes
        s.gate.enabled = (block / 97) % 3 != 0;
        s.gate.sidechainLpfHz = 2000.0f + static_cast<float>(block % 7) * 500.0f; // filter redesigns
        s.gain.bypass = (block / 211) % 4 == 3;
        s.gainOn = (block / 173) % 5 != 2; // Smart Gain module power
        s.drum.debleedEnabled = (block / 53) % 3 != 1;
        s.drum.debleedCrossoverHz = 3000.0f + static_cast<float>(block % 5) * 1000.0f; // crossover redesigns
        s.drum.moduleOn = (block / 61) % 4 != 3;
        s.drum.replaceEnabled = (block / 37) % 3 != 2;                  // replacement on/off (latency changes)
        s.drum.replaceLookaheadMs = (block / 90) % 2 ? 1.5f : 6.0f;      // live / studio
        s.drum.replacePitchSt = static_cast<float>(block % 9) - 4.0f;
        s.drum.replaceThresholdDb = -30.0f;
        s.drum.replaceLengthMs = (block % 2) ? 2000.0f : 120.0f;
        s.eq.enabled = (block / 47) % 3 != 1;                            // EQ on/off
        s.eq.bands[0].on = (block / 29) % 2 == 0;
        s.eq.bands[0].slope = (block / 71) % 2 ? 48.0f : 12.0f;           // section count changes
        s.eq.bands[3].gainDb = static_cast<float>(block % 13) - 6.0f;     // coefficient glides
        s.eq.bands[3].freqHz = 200.0f + static_cast<float>(block % 17) * 300.0f;
        s.comp.enabled = (block / 43) % 3 != 2;                           // compressor on/off
        s.comp.thresholdDb = -30.0f + static_cast<float>(block % 11);
        s.comp.attackMs = (block / 31) % 2 ? 1.0f : 30.0f;               // coefficient changes
        s.comp.scHpfHz = 60.0f + static_cast<float>(block % 6) * 40.0f;   // sidechain filter redesigns
        s.comp.autoRelease = (block / 67) % 2 == 0;
        s.comp.autoMakeup = (block / 59) % 2 == 0;
        s.comp.detector = (block / 23) % 2 ? mixcore::comp::Detector::Peak : mixcore::comp::Detector::Rms;
        s.comp.mixPercent = static_cast<float>(block % 5) * 25.0f;
        if (block == 400)
            strip.debleed().resetLearning();
        float* ptrs[2] = { l.data() + pos, r.data() + pos };
        ScopedRealtimeCheck check;
        strip.setSettings(s);
        strip.process(ptrs, 2, 256);
        strip.gate().readTelemetry(gt);
        strip.debleed().readTelemetry(dt);
        strip.replacer().readTelemetry(rt);
        strip.gain().readTelemetry(st);
        strip.comp().readTelemetry(ct);
        allocations += check.counts().allocations;
        frees += check.counts().deallocations;
    }
    CHECK(allocations == 0);
    CHECK(frees == 0);
    CHECK(strip.gate().learner().state() == mixcore::gate::LearnState::Ready); // learning ran on the audio thread
}

TEST_CASE("Kit swaps while the audio thread plays never touch freed memory", "[realtime][threads][replace]")
{
    // Run under ThreadSanitizer / AddressSanitizer to make this meaningful.
    using namespace mixcore::drum;
    DrumReplacer r;
    REQUIRE(r.prepare(48000.0, 128, 1));
    DrumSettings s;
    s.moduleOn = true;
    s.replaceEnabled = true;
    s.replaceThresholdDb = -30.0f;
    s.replaceRetriggerMs = 5.0f;
    r.setSettings(s);
    r.reset();
    r.kits().publish(makeStarterKit("starter:kick"));

    std::atomic<bool> stop { false };
    std::thread audio([&] {
        std::vector<float> buf(128), key(128);
        int n = 0;
        while (!stop.load())
        {
            for (int i = 0; i < 128; ++i, ++n)
                key[static_cast<size_t>(i)] = buf[static_cast<size_t>(i)] = (n % 2400) < 20 ? 0.8f : 0.0f; // a hit every 50 ms
            float* io[1] = { buf.data() };
            const float* k[1] = { key.data() };
            r.process(io, k, 1, 128);
        }
    });
    const char* ids[] = { "starter:kick", "starter:snare", "starter:tom" };
    for (int i = 0; i < 60; ++i)
    {
        r.kits().publish(makeStarterKit(ids[i % 3]));
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        r.kits().collectGarbage();
    }
    stop = true;
    audio.join();
    r.kits().collectGarbage();
    CHECK(r.kits().retiredCount() <= 2); // freed as the audio thread moved on
}
