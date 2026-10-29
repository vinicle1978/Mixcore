// Long-duration performance test. Reports realtime factor (how many times
// faster than realtime the stage runs) on the machine executing the test.
// Thresholds are deliberately conservative so they hold on shared CI runners;
// the measured figures are what get documented, not the thresholds.

#include "../support/StageHarness.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>

using namespace mixcore;
using namespace mixcore::gain;
using namespace mixcore::test;

namespace {
double measureRealtimeFactor(double sr, int channels, double seconds, bool guard, int block)
{
    SmartGainSettings s;
    s.peakGuardEnabled = guard;
    auto stage = makeStage(sr, s, channels);
    auto mono = compose(sr, { { Segment::Vocal, 8.0, -24.0 }, { Segment::Silence, 1.0 }, { Segment::Vocal, 7.0, -14.0 } },
                        -60.0);
    std::vector<std::vector<float>> buf(static_cast<size_t>(channels), std::vector<float>(static_cast<size_t>(block)));
    std::vector<float*> ptrs(static_cast<size_t>(channels));
    const auto total = static_cast<size_t>(seconds * sr);
    SmartGainTelemetry t;

    const auto t0 = std::chrono::steady_clock::now();
    size_t src = 0;
    for (size_t pos = 0; pos < total; pos += static_cast<size_t>(block))
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            for (int i = 0; i < block; ++i)
                buf[static_cast<size_t>(ch)][static_cast<size_t>(i)] = mono[(src + static_cast<size_t>(i)) % mono.size()];
            ptrs[static_cast<size_t>(ch)] = buf[static_cast<size_t>(ch)].data();
        }
        src += static_cast<size_t>(block);
        stage->process(ptrs.data(), channels, block);
        stage->readTelemetry(t);
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    REQUIRE(std::isfinite(t.outputRmsDb));
    return seconds / elapsed;
}
} // namespace

TEST_CASE("Ten minutes of stereo audio processes far faster than realtime", "[performance]")
{
    const double rtfStereo = measureRealtimeFactor(48000.0, 2, 600.0, false, 256);
    const double rtfGuard = measureRealtimeFactor(48000.0, 2, 600.0, true, 256);
    const double rtf96 = measureRealtimeFactor(96000.0, 2, 120.0, true, 64);
    std::printf("[performance] stereo 48k/256: %.1fx realtime (%.3f%% of one core)\n", rtfStereo, 100.0 / rtfStereo);
    std::printf("[performance] stereo 48k/256 + peak guard: %.1fx realtime (%.3f%% of one core)\n", rtfGuard, 100.0 / rtfGuard);
    std::printf("[performance] stereo 96k/64 + peak guard: %.1fx realtime (%.3f%% of one core)\n", rtf96, 100.0 / rtf96);
    CHECK(rtfStereo > 20.0);
    CHECK(rtfGuard > 20.0);
    CHECK(rtf96 > 10.0);
}
