// Drum replacement tests: timing, detection, velocity, layers, round robin,
// pitch, attack/length, latency, kits. Synthetic signals; real-drum listening
// tests are still required.

#include "../support/DrumSignals.h"

#include "mixcore/drum/DrumReplacer.h"
#include "mixcore/drum/StarterKits.h"
#include "mixcore/strip/ChannelStrip.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace mixcore;
using namespace mixcore::drum;
using namespace mixcore::test;
using Catch::Approx;

namespace {

constexpr double kSr = 48000.0;

SampleBuffer constantSample(float value, size_t frames, double sr = kSr, const char* name = "s")
{
    SampleBuffer b;
    b.name = name;
    b.sampleRate = sr;
    b.left.assign(frames, value);
    return b;
}

/// Kit whose layers are flat "marker" samples, so the output level tells which
/// layer / round robin played.
std::unique_ptr<SampleKit> markerKit(const std::vector<std::vector<float>>& layers, size_t frames = 2400)
{
    auto kit = std::make_unique<SampleKit>();
    kit->id = "test";
    kit->name = "Markers";
    for (const auto& rr : layers)
    {
        SampleLayer l;
        for (float v : rr)
            l.roundRobin.push_back(constantSample(v, frames));
        kit->layers.push_back(std::move(l));
    }
    return kit;
}

std::unique_ptr<SampleKit> impulseKit(size_t frames = 4800)
{
    auto kit = std::make_unique<SampleKit>();
    SampleLayer l;
    auto b = constantSample(0.0f, frames);
    b.left[0] = 1.0f;
    l.roundRobin.push_back(std::move(b));
    kit->layers.push_back(std::move(l));
    return kit;
}

DrumSettings replaceSettings()
{
    DrumSettings s;
    s.moduleOn = true;
    s.replaceEnabled = true;
    s.replaceThresholdDb = -20.0f;
    s.replaceRetriggerMs = 40.0f;
    s.replaceDynamics = 0.0f;
    s.replaceOriginalDb = -60.0f; // pure replacement
    s.replaceSampleDb = 0.0f;
    s.replaceLookaheadMs = 1.5f;
    return s;
}

void setup(DrumReplacer& r, const DrumSettings& s, std::unique_ptr<SampleKit> kit, double sr = kSr, int ch = 1)
{
    REQUIRE(r.prepare(sr, 512, ch));
    r.setSettings(s);
    r.reset();
    r.kits().publish(std::move(kit));
}

Signal run(DrumReplacer& r, const Signal& in, int block = 256, const Signal* key = nullptr)
{
    Signal out = in;
    for (size_t pos = 0; pos < out.size(); pos += static_cast<size_t>(block))
    {
        const int n = static_cast<int>(std::min(out.size() - pos, static_cast<size_t>(block)));
        float* io[1] = { out.data() + pos };
        const float* k[1] = { key != nullptr ? key->data() + pos : out.data() + pos };
        if (key == nullptr)
        {
            // key == audio: copy first (audio is modified in place)
            static thread_local std::vector<float> copy;
            copy.assign(io[0], io[0] + n);
            k[0] = copy.data();
            r.process(io, k, 1, n);
        }
        else
        {
            r.process(io, k, 1, n);
        }
    }
    return out;
}

/// Hits: decaying 150 Hz bursts at `times` (seconds) with peak amplitudes `amps`.
Signal hits(double sr, double seconds, const std::vector<double>& times, const std::vector<float>& amps)
{
    Signal s(static_cast<size_t>(seconds * sr), 0.0f);
    for (size_t h = 0; h < times.size(); ++h)
    {
        const auto start = static_cast<size_t>(times[h] * sr);
        for (size_t k = 0; k < static_cast<size_t>(0.2 * sr) && start + k < s.size(); ++k)
        {
            const double t = static_cast<double>(k) / sr;
            s[start + k] += static_cast<float>(static_cast<double>(amps[h]) * std::exp(-t / 0.05) * std::cos(2.0 * 3.14159265 * 150.0 * t));
        }
    }
    return s;
}

std::vector<size_t> onsets(const Signal& s, float thr = 1.0e-4f, size_t gap = 2400)
{
    std::vector<size_t> out;
    size_t last = 0;
    bool first = true;
    for (size_t i = 0; i < s.size(); ++i)
        if (std::abs(s[i]) > thr && (first || i - last > gap))
        {
            out.push_back(i);
            last = i;
            first = false;
        }
        else if (std::abs(s[i]) > thr)
        {
            last = i;
        }
    return out;
}

float peakIn(const Signal& s, size_t b, size_t e)
{
    float p = 0.0f;
    for (size_t i = b; i < std::min(e, s.size()); ++i)
        p = std::max(p, std::abs(s[i]));
    return p;
}

} // namespace

TEST_CASE("Replacement settings validate and round-trip; old files keep the module on", "[drum][replace]")
{
    DrumSettings s = replaceSettings();
    s.replacePitchSt = 40.0f;
    s.replaceLookaheadMs = std::numeric_limits<float>::infinity();
    s.validate();
    CHECK(s.replacePitchSt == 12.0f);
    CHECK(s.replaceLookaheadMs == DrumSettings {}.replaceLookaheadMs);
    CHECK(DrumSettings::deserialise(s.serialise()) == s);
    // A version-1 string (de-bleed only, no master switch) loads with the module on.
    const auto v1 = DrumSettings::deserialise("version=1;debleedEnabled=1;debleedAmount=40;");
    CHECK(v1.moduleOn);
    CHECK(v1.debleedEnabled);
    CHECK_FALSE(v1.replaceEnabled);
    CHECK_FALSE(DrumSettings {}.moduleOn);
}

TEST_CASE("Each sample starts exactly on the hit, delayed by the reported latency", "[drum][replace][timing]")
{
    DrumReplacer r;
    setup(r, replaceSettings(), impulseKit());
    const int L = r.latencySamples();
    CHECK(L == 72); // 1.5 ms at 48 kHz

    Signal in(48000, 0.0f);
    const size_t t0 = 12345;
    for (size_t k = 0; k < 2000; ++k)
        in[t0 + k] = 0.5f * static_cast<float>(std::exp(-static_cast<double>(k) / 400.0));
    const auto out = run(r, in);
    const auto on = onsets(out);
    REQUIRE(on.size() == 1);
    CHECK(on[0] == t0 + static_cast<size_t>(L));
    CHECK(out[t0 + static_cast<size_t>(L)] == Approx(1.0f).margin(1e-5)); // impulse at full velocity

    // Same with the original kept: original delayed by L plus the sample on top.
    DrumSettings mix = replaceSettings();
    mix.replaceOriginalDb = 0.0f;
    DrumReplacer m;
    setup(m, mix, impulseKit());
    const auto both = run(m, in);
    CHECK(both[t0 + static_cast<size_t>(L)] == Approx(1.0f + in[t0]).margin(1e-4));
    CHECK(both[t0 + static_cast<size_t>(L) + 100] == Approx(in[t0 + 100]).margin(1e-4));
}

TEST_CASE("Key offset keeps samples aligned when the audio lags the key (gate lookahead)", "[drum][replace][timing]")
{
    DrumReplacer r;
    setup(r, replaceSettings(), impulseKit());
    const int L = r.latencySamples();
    const int offset = 72;
    r.setKeyOffset(offset);

    Signal key(48000, 0.0f);
    const size_t t0 = 20000;
    for (size_t k = 0; k < 2000; ++k)
        key[t0 + k] = 0.5f * static_cast<float>(std::exp(-static_cast<double>(k) / 400.0));
    Signal audio(key.size(), 0.0f); // audio = key delayed by `offset`
    for (size_t i = static_cast<size_t>(offset); i < key.size(); ++i)
        audio[i] = key[i - static_cast<size_t>(offset)];

    DrumSettings s = replaceSettings();
    s.replaceOriginalDb = 0.0f;
    r.setSettings(s);
    const auto out = run(r, audio, 256, &key);
    // The original's onset appears at t0 + offset + L; the sample must land on it.
    const size_t expected = t0 + static_cast<size_t>(offset + L);
    CHECK(out[expected - 1] == Approx(0.0f).margin(1e-6));
    CHECK(out[expected] == Approx(1.0f + key[t0]).margin(1e-4));
}

TEST_CASE("Off = bit-exact and zero latency; on/off switches cleanly", "[drum][replace]")
{
    DrumSettings s = replaceSettings();
    s.replaceEnabled = false;
    DrumReplacer r;
    setup(r, s, impulseKit());
    CHECK(r.latencySamples() == 0);
    const auto in = kickGroove(kSr, 2.0, 120.0, 0.6f, -50.0, 1);
    CHECK(run(r, in) == in);

    // Master power off also means off.
    DrumSettings m = replaceSettings();
    m.moduleOn = false;
    DrumReplacer q;
    setup(q, m, impulseKit());
    CHECK(q.latencySamples() == 0);
    CHECK(run(q, in) == in);

    // Turning on mid-stream: latency arrives, output stays finite and bounded.
    q.setSettings(replaceSettings());
    const auto out = run(q, in);
    CHECK(q.latencySamples() == 72);
    for (float v : out)
        REQUIRE(std::isfinite(v));
}

TEST_CASE("One trigger per kick: no misses, no double triggers, bleed ignored", "[drum][replace][detect]")
{
    const double seconds = 8.0;
    const auto in = kickGroove(kSr, seconds, 120.0, 0.6f, -40.0, 7); // 16 kicks + bleed
    DrumSettings s = replaceSettings();
    s.replaceDetectHighHz = 200.0f; // kick detector
    DrumReplacer r;
    setup(r, s, impulseKit());
    run(r, in);
    ReplaceTelemetry t;
    REQUIRE(r.readTelemetry(t));
    CHECK(t.totalHits == 16);
}

TEST_CASE("Retrigger time rejects flams; fast rolls still trigger", "[drum][replace][detect]")
{
    // Two hits 20 ms apart, the second one louder (no dip below the re-arm level in between).
    const auto in = hits(kSr, 1.0, { 0.2, 0.22 }, { 0.3f, 0.6f });
    for (float retrigger : { 40.0f, 10.0f })
    {
        DrumSettings s = replaceSettings();
        s.replaceRetriggerMs = retrigger;
        DrumReplacer r;
        setup(r, s, impulseKit());
        run(r, in);
        ReplaceTelemetry t;
        REQUIRE(r.readTelemetry(t));
        INFO("retrigger " << retrigger);
        CHECK(t.totalHits == (retrigger > 20.0f ? 1u : 2u));
    }
}

TEST_CASE("Velocity follows the playing, or is fixed with Dynamics at 0", "[drum][replace][velocity]")
{
    const std::vector<float> amps { 0.12f, 0.25f, 0.5f, 1.0f };
    const auto in = hits(kSr, 3.0, { 0.25, 0.85, 1.45, 2.05 }, amps);
    for (float dyn : { 100.0f, 0.0f })
    {
        DrumSettings s = replaceSettings();
        s.replaceDynamics = dyn;
        s.replaceRangeDb = 24.0f;
        DrumReplacer r;
        setup(r, s, markerKit({ { 0.5f } }));
        const auto out = run(r, in);
        std::vector<float> peaks;
        for (double t : { 0.25, 0.85, 1.45, 2.05 })
            peaks.push_back(peakIn(out, static_cast<size_t>((t + 0.002) * kSr), static_cast<size_t>((t + 0.04) * kSr)));
        INFO("dynamics " << dyn << ": " << peaks[0] << " " << peaks[1] << " " << peaks[2] << " " << peaks[3]);
        if (dyn > 0.0f)
        {
            CHECK(peaks[0] < peaks[1]);
            CHECK(peaks[1] < peaks[2]);
            CHECK(peaks[2] < peaks[3]);
        }
        else
        {
            for (float p : peaks)
                CHECK(p == Approx(0.5f).margin(1e-4)); // every hit at full velocity
        }
    }
}

TEST_CASE("Velocity layers and round robin", "[drum][replace][layers]")
{
    // Layer 0 = 0.1 (quiet), layer 1 = 0.3, layer 2 = 0.9 with two round-robin variants (0.8, 0.9).
    DrumSettings s = replaceSettings();
    s.replaceDynamics = 100.0f;
    s.replaceRangeDb = 20.0f; // a full-scale hit = velocity 1
    DrumReplacer r;
    setup(r, s, markerKit({ { 0.1f }, { 0.3f }, { 0.8f, 0.9f } }));
    const std::vector<double> times { 0.25, 0.75, 1.25, 1.75 };
    // First hit just over the threshold (quiet), then three full-scale hits.
    const auto in = hits(kSr, 2.5, times, { 0.11f, 1.0f, 1.0f, 1.0f });
    const auto out = run(r, in);
    std::vector<float> p;
    for (double t : times)
        p.push_back(peakIn(out, static_cast<size_t>((t + 0.002) * kSr), static_cast<size_t>((t + 0.04) * kSr)));
    INFO(p[0] << " " << p[1] << " " << p[2] << " " << p[3]);
    CHECK(p[0] < 0.1f);                       // quiet hit -> quietest layer
    CHECK(p[1] == Approx(0.8f).margin(1e-3)); // loud hits -> loudest layer, alternating
    CHECK(p[2] == Approx(0.9f).margin(1e-3));
    CHECK(p[3] == Approx(0.8f).margin(1e-3));
}

TEST_CASE("Pitch and sample-rate conversion", "[drum][replace][pitch]")
{
    auto measureHz = [](const Signal& s, size_t b, size_t e, double sr) {
        int crossings = 0;
        for (size_t i = b + 1; i < e; ++i)
            if ((s[i - 1] < 0.0f) != (s[i] < 0.0f))
                ++crossings;
        return 0.5 * crossings * sr / static_cast<double>(e - b);
    };
    for (auto [pitch, sampleSr, expected] :
         { std::tuple { 0.0f, 48000.0, 1000.0 }, std::tuple { 12.0f, 48000.0, 2000.0 }, std::tuple { -12.0f, 48000.0, 500.0 },
           std::tuple { 0.0f, 44100.0, 1000.0 } })
    {
        auto kit = std::make_unique<SampleKit>();
        SampleLayer l;
        SampleBuffer b;
        b.sampleRate = sampleSr;
        for (int i = 0; i < static_cast<int>(sampleSr); ++i)
            b.left.push_back(0.5f * static_cast<float>(std::sin(2.0 * 3.14159265 * 1000.0 * i / sampleSr)));
        l.roundRobin.push_back(std::move(b));
        kit->layers.push_back(std::move(l));
        DrumSettings s = replaceSettings();
        s.replacePitchSt = pitch;
        DrumReplacer r;
        setup(r, s, std::move(kit));
        const auto out = run(r, hits(kSr, 1.0, { 0.1 }, { 0.5f }));
        const double hz = measureHz(out, static_cast<size_t>(0.2 * kSr), static_cast<size_t>(0.4 * kSr), kSr);
        INFO("pitch " << pitch << " sample rate " << sampleSr << ": " << hz << " Hz");
        CHECK(hz == Approx(expected).epsilon(0.01));
    }
}

TEST_CASE("Attack softens the start; Length shortens the sample", "[drum][replace][shape]")
{
    const auto in = hits(kSr, 1.0, { 0.2 }, { 0.5f });
    const size_t start = static_cast<size_t>(0.2 * kSr) + 72;

    DrumSettings s = replaceSettings();
    s.replaceAttackMs = 10.0f;
    DrumReplacer a;
    setup(a, s, markerKit({ { 0.5f } }, 24000));
    const auto outA = run(a, in);
    CHECK(std::abs(outA[start]) < 0.01f);                               // starts from silence
    CHECK(std::abs(outA[start + 240]) == Approx(0.25f).margin(0.01f));  // half-way up after 5 ms
    CHECK(std::abs(outA[start + 600]) == Approx(0.5f).margin(1e-4));    // fully up

    DrumSettings l = replaceSettings();
    l.replaceLengthMs = 50.0f;
    DrumReplacer b;
    setup(b, l, markerKit({ { 0.5f } }, 24000));
    const auto outB = run(b, in);
    CHECK(std::abs(outB[start + 1000]) == Approx(0.5f).margin(1e-4));   // before the fade
    CHECK(peakIn(outB, start + 2400, start + 20000) < 1e-6f);           // silent after 50 ms
}

TEST_CASE("Replacement output is independent of block size", "[drum][replace]")
{
    const auto in = kickGroove(kSr, 3.0, 140.0, 0.6f, -45.0, 3);
    DrumSettings s = replaceSettings();
    s.replaceOriginalDb = -6.0f;
    s.replaceDynamics = 70.0f;
    DrumReplacer a, b;
    setup(a, s, makeStarterKit("starter:kick"));
    setup(b, s, makeStarterKit("starter:kick"));
    CHECK(run(a, in, 64) == run(b, in, 997));
}

TEST_CASE("Kits: auto layering, describe, starter kits", "[drum][replace][kits]")
{
    std::vector<SampleBuffer> files;
    for (float a : { 0.9f, 0.1f, 0.3f, 0.105f, 0.88f }) // unsorted, with near-duplicates
        files.push_back(constantSample(a, 4800));
    files.push_back(constantSample(0.0f, 4800)); // silent: dropped
    const auto kit = buildKit("files", "Mine", files);
    REQUIRE(kit.layers.size() == 3);
    CHECK(kit.layers[0].roundRobin.size() == 2); // 0.1 and 0.105
    CHECK(kit.layers[1].roundRobin.size() == 1); // 0.3
    CHECK(kit.layers[2].roundRobin.size() == 2); // 0.88 and 0.9
    CHECK(kit.describe() == "3 velocity layers x 1-2 round robin (5 samples)");

    const auto capped = buildKit("files", "Capped", files, 0.1f, 2);
    CHECK(capped.layers.size() == 2);
    CHECK(capped.sampleCount() == 5);

    for (const auto& info : starterKits())
    {
        const auto k = makeStarterKit(info.id);
        REQUIRE(k != nullptr);
        INFO(info.id);
        CHECK(k->layers.size() == 3);
        for (size_t i = 0; i < k->layers.size(); ++i)
        {
            CHECK(k->layers[i].roundRobin.size() == 2);
            if (i > 0)
                CHECK(k->layers[i].loudnessDb > k->layers[i - 1].loudnessDb + 2.0f);
            for (const auto& smp : k->layers[i].roundRobin)
                for (float v : smp.left)
                    REQUIRE(std::isfinite(v));
        }
    }
    CHECK(makeStarterKit("starter:cowbell") == nullptr);
}

TEST_CASE("Kit exchange frees retired kits only after the audio thread moved on", "[drum][replace][kits]")
{
    KitExchange x;
    x.publish(makeStarterKit("starter:kick"));
    const auto* first = x.acquire();
    x.publish(makeStarterKit("starter:snare"));
    CHECK(x.retiredCount() == 1); // still possibly in use
    x.blockDone();
    x.collectGarbage();
    CHECK(x.retiredCount() == 1);
    x.blockDone();
    x.collectGarbage();
    CHECK(x.retiredCount() == 0);
    CHECK(x.acquire() != first);
    CHECK(x.acquire()->name == "Starter Snare");
}

TEST_CASE("In the strip, replacement listens before the gate and adds its latency", "[drum][replace][strip]")
{
    strip::ChannelStrip st;
    REQUIRE(st.prepare(kSr, 256, 1));
    strip::ChannelSettings c;
    c.gainOn = false;
    c.gate.enabled = true;
    c.gate.thresholdDb = -1.0f; // the gate stays closed for these hits...
    c.gate.rangeDb = 80.0f;
    c.gate.lookaheadMs = 1.5f;
    c.drum = replaceSettings();
    c.drum.replaceOriginalDb = 0.0f;
    st.setSettings(c);
    st.reset();
    st.replacer().kits().publish(markerKit({ { 0.5f } }, 4800));
    CHECK(st.latencySamples() == 72 + 72);

    auto audio = hits(kSr, 1.0, { 0.3 }, { 0.4f });
    for (size_t pos = 0; pos < audio.size(); pos += 256)
    {
        float* io[1] = { audio.data() + pos };
        st.process(io, 1, static_cast<int>(std::min<size_t>(256, audio.size() - pos)));
    }
    // ...but the replacement still triggers, aligned with the (gated) original.
    const size_t expected = static_cast<size_t>(0.3 * kSr) + 144;
    CHECK(std::abs(audio[expected - 1]) < 1e-3f);
    CHECK(audio[expected] == Approx(0.5f).margin(2e-3));
}
