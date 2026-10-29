// Smart Gain Stage acceptance tests (spec section 7.5).
// Signals are deterministic and synthetic; musical behaviour must still be
// confirmed by listening tests (docs/testing/listening-tests.md).

#include "../support/StageHarness.h"

#include "mixcore/gain/SmartGainCoach.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace mixcore;
using namespace mixcore::gain;
using namespace mixcore::test;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

bool isActive(const SmartGainTelemetry& t) { return t.activity == ActivityState::Active; }

float expectedRiderTarget(const SmartGainSettings& s, float levelPostStaticDb)
{
    const float e = s.targetRmsDb - levelPostStaticDb;
    const float shaped = std::abs(e) <= s.deadbandDb ? 0.0f : std::copysign(std::abs(e) - s.deadbandDb, e);
    return std::clamp(s.riderStrength * shaped, -std::min(s.riderMaxDownDb, s.riderRangeDb),
                      std::min(s.riderMaxUpDb, s.riderRangeDb));
}
} // namespace

TEST_CASE("Input and output RMS telemetry is accurate", "[gain][rms]")
{
    SmartGainSettings s;
    s.riderEnabled = false;
    s.staticGainDb = 6.0f;
    auto stage = makeStage(kSr, s);

    auto in = sine(kSr, 1000.0, 2.0, 1.0);
    scaleToRmsDb(in, -20.0);
    std::vector<SmartGainTelemetry> ticks;
    processWithTelemetry(*stage, in, ticks);

    const auto& t = ticks.at(tickAt(1.5));
    CHECK(t.inputRmsDb == Approx(-20.0f).margin(0.2));
    CHECK(t.outputRmsDb == Approx(-14.0f).margin(0.2));
    CHECK(t.inputPeakDb == Approx(-16.99f).margin(0.05)); // RMS + 3.01
    CHECK(t.staticGainDb == Approx(6.0f));
    CHECK(t.riderGainDb == 0.0f);
}

TEST_CASE("Static calibration measures the active vocal level", "[gain][calibration]")
{
    SmartGainSettings s; // target -18
    auto stage = makeStage(kSr, s);
    const auto in = compose(kSr,
                            { { Segment::Silence, 0.5 },
                              { Segment::Vocal, 3.0, -27.0 },
                              { Segment::Silence, 0.7 },
                              { Segment::Vocal, 3.0, -27.0 },
                              { Segment::Silence, 0.5 } },
                            -70.0);

    stage->calibrator().requestStart(4.0);
    std::vector<SmartGainTelemetry> ticks;
    processWithTelemetry(*stage, in, ticks);

    REQUIRE(stage->calibrator().state() == CalibrationState::Ready);
    const auto r = stage->calibrator().computeResult(s.targetRmsDb, s.peakRestrictDb);
    INFO(r.summary);
    REQUIRE(r.valid);
    CHECK(r.measuredMedianDb == Approx(-27.0f).margin(0.75));
    CHECK(r.recommendedGainDb == Approx(9.0f).margin(0.75));
    CHECK(r.recommendedGainDb == Approx(s.targetRmsDb - r.measuredMedianDb));
    CHECK_FALSE(r.clampedToRange);
    CHECK_FALSE(r.peakConflict);
    CHECK_FALSE(r.lowConfidence);
    // Calibration must not change anything by itself.
    CHECK(stage->settings().staticGainDb == 0.0f);
    CHECK(ticks.back().staticGainDb == 0.0f);
}

TEST_CASE("Calibration clamps large corrections and flags peak conflicts", "[gain][calibration][safety]")
{
    SmartGainSettings s;
    auto stage = makeStage(kSr, s);
    auto in = compose(kSr, { { Segment::Silence, 0.3 }, { Segment::Vocal, 5.0, -32.0 } }, -75.0);
    for (size_t i = static_cast<size_t>(0.5 * kSr); i < in.size(); i += 9600)
        in[i] = 0.5f; // -6 dBFS clicks: high crest factor

    stage->calibrator().requestStart(3.0);
    processMono(*stage, in, 512);
    REQUIRE(stage->calibrator().state() == CalibrationState::Ready);
    const auto r = stage->calibrator().computeResult(s.targetRmsDb, s.peakRestrictDb);
    INFO(r.summary);
    REQUIRE(r.valid);
    CHECK(r.rawGainDb > 12.0f);
    CHECK(r.recommendedGainDb == 12.0f);
    CHECK(r.clampedToRange);
    CHECK(r.maxInputPeakDb == Approx(-6.02f).margin(0.05));
    CHECK(r.peakConflict);
    CHECK(r.safeGainDb == Approx(3.0f).margin(0.1));
    CHECK(r.summary.find("safer setting") != std::string::npos);
}

TEST_CASE("Calibration does not complete without active vocal", "[gain][calibration]")
{
    SmartGainSettings s;
    auto stage = makeStage(kSr, s);
    stage->calibrator().requestStart(2.0);
    processMono(*stage, noise(kSr, 6.0, -60.0, 7), 480);
    CHECK(stage->calibrator().state() == CalibrationState::Collecting);
    CHECK(stage->calibrator().progress() == 0.0f);
    const auto r = stage->calibrator().computeResult(s.targetRmsDb, s.peakRestrictDb);
    CHECK_FALSE(r.valid);
    stage->calibrator().cancel();
    CHECK(stage->calibrator().state() == CalibrationState::Idle);
}

TEST_CASE("Static gain and rider are separate systems", "[gain][separation]")
{
    SECTION("with the rider at zero strength, output is exactly input x static gain")
    {
        SmartGainSettings s;
        s.staticGainDb = 7.5f;
        s.riderStrength = 0.0f;
        auto stage = makeStage(kSr, s);
        const auto in = compose(kSr, { { Segment::Vocal, 2.0, -30.0 }, { Segment::Silence, 0.5 } }, -70.0);
        const auto out = processMono(*stage, in, 256);
        const auto g = static_cast<float>(dbToGainD(7.5));
        for (size_t i = 0; i < in.size(); ++i)
            REQUIRE(out[i] == in[i] * g);
    }

    SECTION("rider movement never changes the static gain")
    {
        SmartGainSettings s;
        s.staticGainDb = 3.0f;
        auto stage = makeStage(kSr, s);
        const auto in = compose(kSr, { { Segment::Vocal, 3.0, -30.0 }, { Segment::Vocal, 3.0, -12.0 } }, -70.0);
        std::vector<SmartGainTelemetry> ticks;
        processWithTelemetry(*stage, in, ticks);
        float riderMin = 0.0f, riderMax = 0.0f;
        for (const auto& t : ticks)
        {
            REQUIRE(t.staticGainDb == 3.0f);
            riderMin = std::min(riderMin, t.riderGainDb);
            riderMax = std::max(riderMax, t.riderGainDb);
        }
        CHECK(riderMax > 2.0f);  // rider rode up on the quiet section
        CHECK(riderMin < -1.0f); // and down on the loud one
    }

    SECTION("a static gain change is reflected immediately in the rider's view of level")
    {
        // The rider measures post-static level, so raising static gain by 6 dB
        // moves the rider target by strength * 6 dB without touching static gain.
        SmartGainSettings s;
        auto stage = makeStage(kSr, s);
        const auto in = compose(kSr, { { Segment::Vocal, 8.0, -26.0 } }, -70.0);
        std::vector<SmartGainTelemetry> ticks;
        const int hop = hopFor(kSr);
        size_t blocks = 0;
        processMono(*stage, in, hop, [&](size_t) {
            if (++blocks == 400)
            {
                s.staticGainDb = 6.0f;
                stage->setSettings(s);
            }
            SmartGainTelemetry t;
            if (stage->readTelemetry(t))
                ticks.push_back(t);
        });
        const auto& before = ticks.at(tickAt(3.9));
        const auto& after = ticks.at(tickAt(4.2));
        CHECK(after.staticGainDb == 6.0f);
        CHECK(after.riderTargetDb == Approx(before.riderTargetDb - 0.5f * 6.0f).margin(0.35));
    }
}

TEST_CASE("Rider converges to the configured partial correction", "[gain][rider][convergence]")
{
    const float strength = GENERATE(0.5f, 1.0f);
    SmartGainSettings s;
    s.riderStrength = strength;
    auto stage = makeStage(kSr, s);
    const auto in = compose(kSr, { { Segment::Silence, 0.5 }, { Segment::Vocal, 10.0, -24.0 } }, -70.0);
    std::vector<SmartGainTelemetry> ticks;
    const auto out = processMono(*stage, in, hopFor(kSr), [&](size_t) {
        SmartGainTelemetry t;
        if (stage->readTelemetry(t))
            ticks.push_back(t);
    });

    // The robust level estimate wanders slightly with the test vocal's 4 Hz
    // modulation, so compare averages over the settled region (8-10 s) and
    // bound the instantaneous tracking error.
    double meanGain = 0.0, meanTarget = 0.0, meanLevel = 0.0;
    float worstTracking = 0.0f;
    const size_t a = tickAt(8.0), b = tickAt(10.0);
    for (size_t i = a; i <= b; ++i)
    {
        const auto& t = ticks.at(i);
        REQUIRE(isActive(t));
        REQUIRE(t.riderTargetDb == Approx(expectedRiderTarget(s, t.levelEstimateDb)).margin(0.01));
        meanGain += static_cast<double>(t.riderGainDb);
        meanTarget += static_cast<double>(t.riderTargetDb);
        meanLevel += static_cast<double>(t.levelEstimateDb);
        worstTracking = std::max(worstTracking, std::abs(t.riderGainDb - t.riderTargetDb));
    }
    const double n = static_cast<double>(b - a + 1);
    meanGain /= n;
    meanTarget /= n;
    meanLevel /= n;
    INFO("mean level " << meanLevel << " mean rider " << meanGain << " worst tracking " << worstTracking);
    CHECK(meanLevel == Approx(-24.0).margin(0.75));
    CHECK(meanGain == Approx(meanTarget).margin(0.1));
    CHECK(worstTracking < 0.4f);
    if (strength == 0.5f)
        CHECK(meanGain == Approx(2.25).margin(0.5));
    else
        CHECK(meanGain == Approx(4.0).margin(0.01)); // max upward correction
    const auto& t = ticks.at(b);

    // Output level moves towards target by the rider gain.
    const auto start = static_cast<size_t>(8.0 * kSr), end = static_cast<size_t>(10.0 * kSr);
    CHECK(rmsDb(out, start, end) == Approx(rmsDb(in, start, end) + static_cast<double>(t.riderGainDb)).margin(0.1));
}

TEST_CASE("Deviations inside the deadband are ignored", "[gain][rider][deadband]")
{
    SmartGainSettings s; // deadband 1.5
    auto stage = makeStage(kSr, s);
    // -18.6 dB RMS: the robust level estimate sits ~0.5 dB lower, still inside the deadband.
    const auto in = compose(kSr, { { Segment::Silence, 0.3 }, { Segment::Vocal, 6.0, -18.6 } }, -70.0);
    std::vector<SmartGainTelemetry> ticks;
    processWithTelemetry(*stage, in, ticks);
    for (const auto& t : ticks)
    {
        if (t.targetErrorValid)
            REQUIRE(std::abs(s.targetRmsDb - t.levelEstimateDb) <= s.deadbandDb);
        REQUIRE(t.riderGainDb == 0.0f);
    }
}

TEST_CASE("Rider gain respects the maximum rate of change", "[gain][rider][rate]")
{
    const float rate = GENERATE(6.0f, 2.0f);
    SmartGainSettings s;
    s.riderStrength = 1.0f;
    s.riderMaxRateDbPerSec = rate;
    s.riderAttackMs = 10.0f; // make the slew limiter, not the smoother, the bottleneck
    s.riderReleaseMs = 10.0f;
    auto stage = makeStage(kSr, s);
    const auto in = compose(kSr, { { Segment::Vocal, 4.0, -30.0 }, { Segment::Vocal, 4.0, -8.0 } }, -70.0);
    std::vector<SmartGainTelemetry> ticks;
    processWithTelemetry(*stage, in, ticks);

    float maxSlope = 0.0f;
    for (size_t i = 1; i < ticks.size(); ++i)
        maxSlope = std::max(maxSlope, std::abs(ticks[i].riderGainDb - ticks[i - 1].riderGainDb) / 0.01f);
    INFO("max slope " << maxSlope << " dB/s");
    CHECK(maxSlope <= rate * 1.001f);
    CHECK(maxSlope >= rate * 0.95f); // and the limit was actually exercised
}

TEST_CASE("Silence, room noise and bleed are never boosted", "[gain][rider][noise]")
{
    SECTION("digital silence")
    {
        auto stage = makeStage(kSr, SmartGainSettings {});
        std::vector<SmartGainTelemetry> ticks;
        const auto out = processWithTelemetry(*stage, Signal(static_cast<size_t>(5 * kSr), 0.0f), ticks);
        for (const auto& t : ticks)
        {
            REQUIRE(t.riderGainDb == 0.0f);
            REQUIRE(t.activity == ActivityState::Inactive);
            REQUIRE(std::isfinite(t.inputRmsDb));
        }
        CHECK(*std::max_element(out.begin(), out.end()) == 0.0f);
    }

    SECTION("continuous room noise and instrument bleed below the vocal")
    {
        const double floorDb = GENERATE(-65.0, -50.0, -42.0);
        auto stage = makeStage(kSr, SmartGainSettings {});
        std::vector<SmartGainTelemetry> ticks;
        processWithTelemetry(*stage, noise(kSr, 20.0, floorDb, 11), ticks);
        size_t activeTicks = 0;
        for (const auto& t : ticks)
        {
            activeTicks += isActive(t) ? 1u : 0u;
            REQUIRE(t.riderGainDb == 0.0f);
        }
        CHECK(activeTicks == 0);
    }

    SECTION("noise after a phrase: rider freezes and never rises")
    {
        auto stage = makeStage(kSr, SmartGainSettings {});
        const auto in = compose(kSr, { { Segment::Vocal, 4.0, -27.0 }, { Segment::Silence, 10.0 } }, -55.0);
        std::vector<SmartGainTelemetry> ticks;
        processWithTelemetry(*stage, in, ticks);
        const size_t phraseEnd = tickAt(4.0);
        REQUIRE(ticks.at(phraseEnd - 10).riderGainDb > 1.0f); // it was riding up
        // After the 100 ms detector window has cleared the phrase tail, the
        // vocal is never considered active again and the gain never rises.
        for (size_t i = tickAt(4.15); i < ticks.size(); ++i)
        {
            REQUIRE_FALSE(isActive(ticks[i]));
            REQUIRE(ticks[i].riderGainDb <= ticks[i - 1].riderGainDb);
        }
        // Held through a short gap...
        CHECK(ticks.at(tickAt(5.9)).riderGainDb == Approx(ticks.at(tickAt(4.3)).riderGainDb).margin(1e-4));
        // ...then released slowly towards 0 dB after 2 s idle, at <= 1 dB/s.
        const float heldGain = ticks.at(tickAt(4.3)).riderGainDb;
        CHECK(ticks.at(tickAt(9.0)).riderGainDb < heldGain - 2.0f);
        CHECK(ticks.at(tickAt(9.0)).riderGainDb >= heldGain - 1.01f * (9.0f - 6.15f));
        CHECK(ticks.back().riderGainDb == Approx(0.0f).margin(1e-4));
    }

    SECTION("negative rider gain is held through long silence (releasing it would raise the noise)")
    {
        auto stage = makeStage(kSr, SmartGainSettings {});
        const auto in = compose(kSr, { { Segment::Vocal, 4.0, -10.0 }, { Segment::Silence, 8.0 } }, -60.0);
        std::vector<SmartGainTelemetry> ticks;
        processWithTelemetry(*stage, in, ticks);
        const float held = ticks.at(tickAt(4.3)).riderGainDb;
        CHECK(held < -2.0f);
        CHECK(ticks.back().riderGainDb == Approx(held).margin(1e-4));
    }

    SECTION("a vocal above continuous bleed is still detected and ridden")
    {
        auto stage = makeStage(kSr, SmartGainSettings {});
        auto bleed = vocalPhrase(kSr, 14.0, -42.0, 110.0); // sustained instrument bleed
        auto vox = compose(kSr, { { Segment::Silence, 6.0 }, { Segment::Vocal, 8.0, -26.0 } });
        Signal in(bleed.size());
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = bleed[i] + (i < vox.size() ? vox[i] : 0.0f);
        std::vector<SmartGainTelemetry> ticks;
        processWithTelemetry(*stage, in, ticks);
        for (size_t i = 0; i < tickAt(6.0); ++i)
            REQUIRE(ticks[i].riderGainDb == 0.0f); // bleed alone never boosted
        CHECK(isActive(ticks.at(tickAt(13.5))));
        CHECK(ticks.at(tickAt(13.5)).riderGainDb > 1.5f);
    }
}

TEST_CASE("Rider recovers on a quiet phrase after a loud one", "[gain][rider][recovery]")
{
    SmartGainSettings s;
    auto stage = makeStage(kSr, s);
    const auto in = compose(kSr,
                            { { Segment::Vocal, 5.0, -12.0 }, { Segment::Silence, 1.0 }, { Segment::Vocal, 6.0, -27.0 } },
                            -70.0);
    std::vector<SmartGainTelemetry> ticks;
    processWithTelemetry(*stage, in, ticks);

    const auto& loud = ticks.at(tickAt(5.0));
    CHECK(loud.riderGainDb == Approx(expectedRiderTarget(s, loud.levelEstimateDb)).margin(0.3));
    CHECK(loud.riderGainDb < -2.0f);

    // Frozen through the gap.
    for (size_t i = tickAt(5.3); i < tickAt(6.0); ++i)
        REQUIRE(ticks[i].riderGainDb == Approx(ticks[tickAt(5.3)].riderGainDb).margin(1e-4));

    // Within 2.5 s of the quiet phrase starting, the rider is within 0.5 dB of
    // its new target (a ~6.5 dB swing at <= 6 dB/s).
    const auto& recovered = ticks.at(tickAt(8.5));
    const float target = expectedRiderTarget(s, recovered.levelEstimateDb);
    CHECK(target > 2.5f);
    CHECK(recovered.riderGainDb == Approx(target).margin(0.5));
}

TEST_CASE("Plosives, clicks and transients do not move the rider", "[gain][rider][transient]")
{
    SmartGainSettings s;
    auto stage = makeStage(kSr, s);
    auto in = compose(kSr, { { Segment::Silence, 0.3 }, { Segment::Vocal, 8.0, -18.0 } }, -70.0);
    // 15 ms 80 Hz "plosive" thumps peaking near 0 dBFS every 600 ms, plus clicks.
    for (double t0 = 1.0; t0 < 8.0; t0 += 0.6)
    {
        const auto start = static_cast<size_t>(t0 * kSr);
        for (size_t k = 0; k < static_cast<size_t>(0.015 * kSr); ++k)
        {
            const double t = static_cast<double>(k) / kSr;
            in[start + k] += static_cast<float>(0.9 * std::exp(-t / 0.005) * std::sin(2 * kPi * 80 * t));
        }
        in[start + 4000] += 0.8f;
    }
    std::vector<SmartGainTelemetry> ticks;
    processWithTelemetry(*stage, in, ticks);
    float worst = 0.0f;
    for (const auto& t : ticks)
        worst = std::max(worst, std::abs(t.riderGainDb));
    INFO("largest rider excursion " << worst << " dB");
    CHECK(worst <= 0.5f);
}

TEST_CASE("Peak safety", "[gain][safety]")
{
    SECTION("rider upward gain is restricted by predicted peaks")
    {
        SmartGainSettings s; // restrict at -3 dBFS
        auto stage = makeStage(kSr, s);
        auto in = compose(kSr, { { Segment::Silence, 0.3 }, { Segment::Vocal, 8.0, -28.0 } }, -70.0);
        for (size_t i = static_cast<size_t>(0.5 * kSr); i < in.size(); i += 4800)
            in[i] = 0.6f; // -4.4 dBFS clicks every 100 ms
        std::vector<SmartGainTelemetry> ticks;
        const auto out = processWithTelemetry(*stage, in, ticks);
        const auto& t = ticks.at(tickAt(8.0));
        const float headroom = s.peakRestrictDb - gainToDb(0.6f);
        CHECK(t.peakRestrictionActive);
        CHECK(t.riderTargetDb == Approx(headroom).margin(0.05));
        CHECK(t.riderGainDb <= headroom + 0.01f);
        CHECK(t.riderRestrictionDb == Approx(4.0f - headroom).margin(0.1));
        float outPeak = 0.0f;
        for (float v : out)
            outPeak = std::max(outPeak, std::abs(v));
        CHECK(gainToDb(outPeak) <= s.peakRestrictDb + 0.05f);
    }

    SECTION("optional lookahead guard holds the output ceiling and reports latency")
    {
        SmartGainSettings s;
        s.staticGainDb = 12.0f;
        s.peakGuardEnabled = true;
        s.peakGuardCeilingDb = -3.0f;
        auto stage = makeStage(kSr, s);
        REQUIRE(stage->latencySamples() == msToSamples(2.0, kSr));
        auto in = compose(kSr, { { Segment::Vocal, 4.0, -16.0 } }, -70.0);
        std::vector<SmartGainTelemetry> ticks;
        const auto out = processWithTelemetry(*stage, in, ticks);
        float outPeak = 0.0f;
        for (float v : out)
            outPeak = std::max(outPeak, std::abs(v));
        CHECK(outPeak <= dbToGain(-3.0f) * 1.00001f);
        float maxReduction = 0.0f;
        bool reported = false;
        for (const auto& t : ticks)
        {
            maxReduction = std::max(maxReduction, t.peakGuardReductionDb);
            reported |= t.peakGuardActive;
            REQUIRE(t.latencySamples == stage->latencySamples());
            REQUIRE(t.peakGuardEnabled);
        }
        CHECK(reported);
        CHECK(maxReduction > 3.0f);
    }

    SECTION("without the guard nothing clips silently: warnings are raised instead")
    {
        SmartGainSettings s;
        s.riderEnabled = false;
        auto stage = makeStage(kSr, s);
        std::vector<SmartGainTelemetry> ticks;
        const auto in = sine(kSr, 500.0, 1.0, static_cast<double>(dbToGain(-4.0f)));
        const auto out = processWithTelemetry(*stage, in, ticks);
        CHECK(ticks.back().peakWarning);
        CHECK_FALSE(ticks.back().peakGuardActive);
        CHECK(out == in); // no hidden clipper
        const auto advice = evaluateSmartGainAdvice(ticks.back(), s);
        CHECK(std::any_of(advice.begin(), advice.end(), [](const Advice& a) { return a.id == "peak-warning"; }));
    }

    SECTION("guard can be toggled live with a click-free transition and correct latency")
    {
        SmartGainSettings s;
        auto stage = makeStage(kSr, s);
        auto in = sine(kSr, 300.0, 1.0, 0.25);
        const int hop = hopFor(kSr);
        int block = 0;
        const auto out = processMono(*stage, in, hop, [&](size_t) {
            if (++block == 20)
            {
                s.peakGuardEnabled = true;
                stage->setSettings(s);
            }
        });
        CHECK(stage->latencySamples() == msToSamples(2.0, kSr));
        float maxJump = 0.0f;
        for (size_t i = 1; i < out.size(); ++i)
            maxJump = std::max(maxJump, std::abs(out[i] - out[i - 1]));
        const float sineMaxStep = 0.25f * static_cast<float>(2 * kPi * 300.0 / kSr);
        CHECK(maxJump <= sineMaxStep * 1.5f);
    }
}

TEST_CASE("Input clipping is detected and reported", "[gain][clipping]")
{
    auto stage = makeStage(kSr, SmartGainSettings {});
    std::vector<SmartGainTelemetry> ticks;

    auto hot = sine(kSr, 220.0, 1.0, 1.5);
    for (auto& v : hot)
        v = std::clamp(v, -1.0f, 1.0f); // interface-clipped signal
    processWithTelemetry(*stage, hot, ticks);
    CHECK(ticks.back().inputClipping);
    CHECK(ticks.back().inputClipEvents >= 400);
    const auto advice = evaluateSmartGainAdvice(ticks.back(), SmartGainSettings {});
    REQUIRE_FALSE(advice.empty());
    CHECK(advice.front().id == "input-clipping");
    CHECK_FALSE(advice.front().changesAudioAutomatically);

    auto clean = makeStage(kSr, SmartGainSettings {});
    processWithTelemetry(*clean, sine(kSr, 220.0, 1.0, 0.99), ticks);
    CHECK_FALSE(ticks.back().inputClipping);
    CHECK(ticks.back().inputClipEvents == 0);
}

TEST_CASE("Stereo channels receive identical (linked) gain", "[gain][stereo]")
{
    SmartGainSettings s;
    s.staticGainDb = 4.0f;
    auto stage = makeStage(kSr, s, 2);
    const auto left = compose(kSr, { { Segment::Vocal, 3.0, -30.0 }, { Segment::Vocal, 3.0, -14.0 } }, -70.0);
    std::vector<Signal> ch { left, Signal(left.size()) };
    for (size_t i = 0; i < left.size(); ++i)
        ch[1][i] = (i % 2 ? -0.5f : 0.5f) * left[i] + 0.01f * left[(i + 37) % left.size()];
    const auto rightIn = ch[1];
    runStage(*stage, ch, 333);
    for (size_t i = 0; i < left.size(); ++i)
    {
        if (std::abs(left[i]) > 1e-4f && std::abs(rightIn[i]) > 1e-4f)
            REQUIRE(ch[0][i] / left[i] == Approx(ch[1][i] / rightIn[i]).epsilon(1e-5));
    }
}

TEST_CASE("Rider envelope is consistent across sample rates", "[gain][samplerate]")
{
    const std::vector<double> rates { 44100.0, 48000.0, 88200.0, 96000.0 };
    std::vector<std::vector<float>> curves;
    for (double sr : rates)
    {
        SmartGainSettings s;
        auto stage = makeStage(sr, s);
        const auto in = compose(sr,
                                { { Segment::Silence, 0.5 },
                                  { Segment::Vocal, 3.5, -28.0 },
                                  { Segment::Silence, 0.5 },
                                  { Segment::Vocal, 3.5, -10.0 } },
                                -70.0);
        std::vector<SmartGainTelemetry> ticks;
        processWithTelemetry(*stage, in, ticks);
        std::vector<float> curve;
        for (double t = 1.0; t < 8.0; t += 0.25)
            curve.push_back(ticks.at(tickAt(t)).riderGainDb);
        curves.push_back(curve);
    }
    for (size_t r = 1; r < curves.size(); ++r)
        for (size_t i = 0; i < curves[0].size(); ++i)
        {
            INFO("rate " << rates[r] << " point " << i);
            REQUIRE(curves[r][i] == Approx(curves[1][i]).margin(0.15));
        }
}

TEST_CASE("Output is identical for any host block size", "[gain][blocksize]")
{
    const bool guard = GENERATE(false, true);
    SmartGainSettings s;
    s.peakGuardEnabled = guard;
    s.staticGainDb = guard ? 10.0f : 0.0f;
    const auto in = compose(kSr, { { Segment::Vocal, 2.5, -26.0 }, { Segment::Silence, 0.4 }, { Segment::Vocal, 2.5, -12.0 } },
                            -65.0);
    Signal reference;
    for (int block : { 4096, 1, 17, 64, 480, 1023 })
    {
        auto stage = makeStage(kSr, s);
        const auto out = processMono(*stage, in, block);
        if (reference.empty())
            reference = out;
        else
            REQUIRE(out == reference);
    }
}

TEST_CASE("Automation stress: random settings, block sizes and hostile input", "[gain][stress]")
{
    Rng rng(2026);
    auto uni = [&](float lo, float hi) { return lo + (hi - lo) * 0.5f * (rng.bipolar() + 1.0f); };

    auto stage = makeStage(kSr, SmartGainSettings {}, 2);
    std::vector<float> l(4096), r(4096);
    float* ptrs[2] = { l.data(), r.data() };
    size_t processed = 0, nonFinite = 0;
    float maxAbs = 0.0f;
    SmartGainTelemetry t;
    while (processed < static_cast<size_t>(30 * kSr))
    {
        SmartGainSettings s;
        s.targetRmsDb = uni(-60.0f, 10.0f); // deliberately out of range at times
        s.staticGainDb = uni(-30.0f, 30.0f);
        s.riderEnabled = rng.bipolar() > -0.8f;
        s.riderStrength = uni(-0.5f, 1.5f);
        s.riderAttackMs = uni(-10.0f, 3000.0f);
        s.riderReleaseMs = uni(0.0f, 6000.0f);
        s.riderMaxRateDbPerSec = uni(0.0f, 100.0f);
        s.deadbandDb = rng.bipolar() > 0.9f ? std::nanf("") : uni(0.0f, 8.0f);
        s.peakGuardEnabled = rng.bipolar() > 0.0f;
        s.bypass = rng.bipolar() > 0.8f;
        stage->setSettings(s);

        const int n = 1 + static_cast<int>((rng.next() >> 33) % 2048);
        const float level = uni(0.0f, 4.0f);
        for (int i = 0; i < n; ++i)
        {
            l[static_cast<size_t>(i)] = level * rng.bipolar();
            r[static_cast<size_t>(i)] = level * rng.bipolar();
        }
        if (rng.bipolar() > 0.95f)
            l[0] = std::numeric_limits<float>::quiet_NaN();
        if (rng.bipolar() > 0.97f)
            r[static_cast<size_t>(n - 1)] = std::numeric_limits<float>::infinity();

        stage->process(ptrs, 2, n);
        for (int i = 0; i < n; ++i)
            for (float v : { l[static_cast<size_t>(i)], r[static_cast<size_t>(i)] })
            {
                nonFinite += std::isfinite(v) ? 0u : 1u;
                maxAbs = std::max(maxAbs, std::abs(v));
            }
        stage->readTelemetry(t);
        processed += static_cast<size_t>(n);
    }
    CHECK(nonFinite == 0);
    // Max possible gain: +18 static + 12 rider = +30 dB on a 4.0 input.
    CHECK(maxAbs <= 4.0f * dbToGain(30.0f) * 1.001f);
    CHECK(t.nonFiniteInputSamples > 0);
    CHECK(std::isfinite(t.riderGainDb));
    CHECK(std::isfinite(t.outputTruePeakDb));
}

TEST_CASE("Restored settings reproduce identical processing", "[gain][state]")
{
    SmartGainSettings s;
    s.staticGainDb = 5.5f;
    s.riderStrength = 0.7f;
    s.targetRmsDb = -20.0f;
    s.peakGuardEnabled = true;
    const auto in = compose(kSr, { { Segment::Vocal, 3.0, -25.0 } }, -68.0);

    auto a = makeStage(kSr, s);
    const auto outA = processMono(*a, in, 256);
    auto b = makeStage(kSr, SmartGainSettings::deserialise(s.serialise()));
    const auto outB = processMono(*b, in, 256);
    CHECK(outA == outB);
}

TEST_CASE("Bypass is bit-transparent and latency-aligned", "[gain][bypass]")
{
    const auto in = compose(kSr, { { Segment::Vocal, 2.0, -30.0 } }, -60.0);

    SECTION("bypassed from the start")
    {
        SmartGainSettings s;
        s.staticGainDb = 9.0f;
        s.bypass = true;
        auto stage = makeStage(kSr, s);
        CHECK(processMono(*stage, in, 512) == in);
    }

    SECTION("bypassed with the guard on: dry signal delayed by the reported latency")
    {
        SmartGainSettings s;
        s.staticGainDb = 9.0f;
        s.bypass = true;
        s.peakGuardEnabled = true;
        auto stage = makeStage(kSr, s);
        const auto L = static_cast<size_t>(stage->latencySamples());
        const auto out = processMono(*stage, in, 512);
        for (size_t i = L; i < in.size(); ++i)
            REQUIRE(out[i] == in[i - L]);
    }

    SECTION("engaging bypass mid-stream crossfades within 10 ms, then is exact")
    {
        SmartGainSettings s;
        s.staticGainDb = 9.0f;
        auto stage = makeStage(kSr, s);
        const int hop = hopFor(kSr);
        int blocks = 0;
        const auto out = processMono(*stage, in, hop, [&](size_t) {
            if (++blocks == 50)
            {
                s.bypass = true;
                stage->setSettings(s);
            }
        });
        const size_t settled = static_cast<size_t>(50 * hop) + static_cast<size_t>(0.0101 * kSr);
        for (size_t i = settled; i < in.size(); ++i)
            REQUIRE(out[i] == in[i]);
    }
}

TEST_CASE("Mix Coach explains every warning", "[gain][coach]")
{
    SmartGainTelemetry t;
    t.inputClipping = true;
    t.peakGuardActive = true;
    t.peakGuardReductionDb = 4.0f;
    t.peakRestrictionActive = true;
    t.riderRestrictionDb = 2.0f;
    t.activity = ActivityState::Active;
    t.inputRmsDb = -50.0f;
    t.calibration = CalibrationState::Collecting;
    const auto advice = evaluateSmartGainAdvice(t, SmartGainSettings {});
    CHECK(advice.size() >= 5);
    for (const auto& a : advice)
    {
        CHECK_FALSE(a.id.empty());
        CHECK_FALSE(a.detected.empty());
        CHECK_FALSE(a.whyItMatters.empty());
        CHECK_FALSE(a.whatToCheck.empty());
        CHECK_FALSE(a.recommendation.empty());
        CHECK_FALSE(a.automaticNote.empty());
    }
}

TEST_CASE("Published peaks survive slow telemetry readers", "[gain][telemetry]")
{
    SmartGainSettings s;
    s.riderEnabled = false;
    auto stage = makeStage(kSr, s);
    Signal in(static_cast<size_t>(kSr), 0.0f);
    in[static_cast<size_t>(0.5 * kSr)] = 0.5f; // single-sample -6 dBFS spike
    // Read only every 40 ms (25 Hz UI): the spike must still be seen.
    float seenIn = -200.0f, seenOut = -200.0f;
    processMono(*stage, in, msToSamples(40.0, kSr), [&](size_t) {
        SmartGainTelemetry t;
        stage->readTelemetry(t);
        seenIn = std::max(seenIn, t.inputPeakDb);
        seenOut = std::max(seenOut, t.outputPeakDb);
    });
    CHECK(seenIn == Approx(-6.02f).margin(0.01));
    CHECK(seenOut == Approx(-6.02f).margin(0.01));
}
