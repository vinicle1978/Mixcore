// Unit tests for engine primitives: RMS window, true-peak estimator,
// lookahead peak guard, triple buffer, settings validation/serialisation.

#include "../support/TestSignals.h"

#include "mixcore/core/DbMath.h"
#include "mixcore/core/TripleBuffer.h"
#include "mixcore/dsp/LookaheadPeakGuard.h"
#include "mixcore/dsp/RollingPower.h"
#include "mixcore/dsp/TruePeakEstimator.h"
#include "mixcore/gain/SmartGainSettings.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <limits>

using namespace mixcore;
using namespace mixcore::test;
using Catch::Approx;

TEST_CASE("dB conversions are consistent and finite", "[dsp][units]")
{
    CHECK(dbToGain(0.0f) == Approx(1.0f));
    CHECK(dbToGain(-6.0206f) == Approx(0.5f).margin(1e-4));
    CHECK(gainToDb(0.5f) == Approx(-6.0206f).margin(1e-3));
    CHECK(gainToDb(0.0f) == kSilenceDb);
    CHECK(powerToDb(0.0) == kSilenceDb);
    CHECK(powerToDb(0.5) == Approx(-3.0103f).margin(1e-3)); // full-scale sine RMS
    CHECK(clampFinite(std::numeric_limits<float>::quiet_NaN(), -1.0f, 1.0f, 0.25f) == 0.25f);
    CHECK(clampFinite(5.0f, -1.0f, 1.0f, 0.0f) == 1.0f);
}

TEST_CASE("RollingPower matches a reference RMS within 0.2 dB once filled", "[dsp][rms]")
{
    const double sr = 48000.0;
    const int window = msToSamples(400.0, sr);
    dsp::RollingPower rp;
    rp.prepare(window);

    // Noise whose level changes every second, including a loud->silent edge.
    Signal s;
    for (double level : { -12.0, -40.0, -3.0, -150.0, -24.0 })
    {
        auto seg = level > -140.0 ? noise(sr, 1.0, level, static_cast<std::uint64_t>(-level * 10 + 3))
                                  : Signal(static_cast<size_t>(sr), 0.0f);
        s.insert(s.end(), seg.begin(), seg.end());
    }

    double worst = 0.0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        rp.push(static_cast<double>(s[i]) * static_cast<double>(s[i]));
        if (i + 1 >= static_cast<size_t>(window) && i % 997 == 0)
        {
            const double ref = rmsDb(s, i + 1 - static_cast<size_t>(window), i + 1);
            const auto got = static_cast<double>(powerToDb(rp.meanPower()));
            if (ref > -140.0)
                worst = std::max(worst, std::abs(ref - got));
            else
                CHECK(got < -140.0); // silence after loud audio really reads as silence (no drift)
        }
    }
    INFO("worst error " << worst << " dB");
    CHECK(rp.isFilled());
    CHECK(worst < 0.2);
    CHECK(worst < 1.0e-6); // in practice it is exact to double precision
}

TEST_CASE("True-peak estimator finds inter-sample peaks", "[dsp][truepeak]")
{
    const double sr = 48000.0;
    // fs/4 sine sampled at +/-45 degrees: sample peak is 3 dB below the true peak.
    const auto s = sine(sr, sr / 4.0, 0.5, 0.5, kPi / 4.0);
    dsp::TruePeakEstimator tp;
    tp.prepare(1);
    float samplePeak = 0.0f, truePeak = 0.0f;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const float t = tp.process(0, s[i]);
        if (i > 64)
        {
            samplePeak = std::max(samplePeak, std::abs(s[i]));
            truePeak = std::max(truePeak, t);
        }
    }
    CHECK(gainToDb(samplePeak) == Approx(-9.03f).margin(0.05));
    CHECK(gainToDb(truePeak) == Approx(-6.02f).margin(0.5));

    // Low-frequency content: true peak ~= sample peak.
    const auto lf = sine(sr, 997.0, 0.5, 0.5);
    tp.reset();
    float lfTrue = 0.0f;
    for (size_t i = 64; i < lf.size(); ++i)
        lfTrue = std::max(lfTrue, tp.process(0, lf[i]));
    CHECK(gainToDb(lfTrue) == Approx(-6.02f).margin(0.1));
}

TEST_CASE("Lookahead peak guard never exceeds the ceiling and reports exact latency", "[dsp][limiter][safety]")
{
    const double sr = 48000.0;
    const int L = msToSamples(2.0, sr);
    dsp::LookaheadPeakGuard guard;
    guard.prepare(2, L, sr);
    guard.setCeilingDb(-3.0f);
    const float ceiling = dbToGain(-3.0f);
    REQUIRE(guard.latencySamples() == L);

    SECTION("material below the ceiling passes unchanged, delayed by L")
    {
        const auto s = sine(sr, 440.0, 0.2, 0.5);
        std::vector<float> outL;
        for (float x : s)
        {
            float frame[2] = { x, -x };
            const float g = guard.processFrame(frame, 2);
            CHECK(g == 1.0f);
            outL.push_back(frame[0]);
        }
        for (size_t i = static_cast<size_t>(L); i < s.size(); ++i)
            REQUIRE(outL[i] == s[i - static_cast<size_t>(L)]);
    }

    SECTION("hot bursts and single-sample spikes are held at the ceiling with no gain steps")
    {
        Rng rng(42);
        auto s = sine(sr, 220.0, 2.0, 0.6);
        for (size_t i = 0; i < s.size(); ++i)
        {
            if ((i / 4800) % 3 == 1)
                s[i] *= 4.0f; // +12 dB bursts
            if (i % 7919 == 0)
                s[i] = (rng.bipolar() > 0 ? 1.0f : -1.0f) * 3.0f; // isolated spikes
        }
        float maxOut = 0.0f, prevGain = 1.0f, maxStep = 0.0f;
        for (float x : s)
        {
            float frame[2] = { x, 0.5f * x };
            const float g = guard.processFrame(frame, 2);
            maxOut = std::max({ maxOut, std::abs(frame[0]), std::abs(frame[1]) });
            maxStep = std::max(maxStep, prevGain - g);
            prevGain = g;
        }
        CHECK(maxOut <= ceiling * 1.000001f);
        // Gain falls along a ramp over L samples: the worst single-sample drop
        // is bounded by (1 - minGain) / L.
        CHECK(maxStep <= (1.0f - ceiling / 3.0f) / static_cast<float>(L) + 1e-6f);
    }
}

TEST_CASE("TripleBuffer delivers the latest complete snapshot", "[core][shared-state]")
{
    struct Snap
    {
        int a;
        int b;
    };
    TripleBuffer<Snap> tb;
    Snap out {};
    CHECK_FALSE(tb.read(out));
    tb.write({ 1, 1 });
    tb.write({ 2, 2 });
    CHECK(tb.read(out));
    CHECK(out.a == 2);
    CHECK(out.b == 2);
    CHECK_FALSE(tb.read(out)); // nothing new
    CHECK(out.a == 2);
    tb.write({ 3, 3 });
    CHECK(tb.read(out));
    CHECK(out.a == 3);
}

TEST_CASE("Settings validation clamps ranges and rejects non-finite values", "[gain][settings]")
{
    gain::SmartGainSettings s;
    s.targetRmsDb = 10.0f;
    s.staticGainDb = -100.0f;
    s.riderStrength = std::numeric_limits<float>::infinity();
    s.deadbandDb = std::numeric_limits<float>::quiet_NaN();
    s.validate();
    CHECK(s.targetRmsDb == -6.0f);
    CHECK(s.staticGainDb == -18.0f);
    CHECK(s.riderStrength == gain::SmartGainSettings {}.riderStrength);
    CHECK(s.deadbandDb == gain::SmartGainSettings {}.deadbandDb);
}

TEST_CASE("Settings state save and restore round-trips exactly", "[gain][settings][state]")
{
    gain::SmartGainSettings s;
    s.targetRmsDb = -21.5f;
    s.staticGainDb = 7.25f;
    s.riderEnabled = false;
    s.riderStrength = 0.8f;
    s.riderAttackMs = 333.0f;
    s.peakGuardEnabled = true;
    s.peakGuardCeilingDb = -1.5f;
    s.bypass = true;

    const auto restored = gain::SmartGainSettings::deserialise(s.serialise());
    CHECK(restored == s);

    SECTION("missing and unknown keys fall back to defaults safely")
    {
        const auto partial = gain::SmartGainSettings::deserialise("version=7;staticGainDb=3;futureParam=12;bogus;riderStrength=abc;");
        CHECK(partial.staticGainDb == 3.0f);
        CHECK(partial.riderStrength == gain::SmartGainSettings {}.riderStrength);
        CHECK(partial.targetRmsDb == gain::SmartGainSettings {}.targetRmsDb);
    }
    SECTION("out-of-range stored values are clamped on load")
    {
        const auto hot = gain::SmartGainSettings::deserialise("staticGainDb=99;targetRmsDb=nan;");
        CHECK(hot.staticGainDb == 18.0f);
        CHECK(hot.targetRmsDb == gain::SmartGainSettings {}.targetRmsDb);
    }
}
