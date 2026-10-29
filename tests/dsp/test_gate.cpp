// Gate / expander tests (spec section 8): triggering, false triggers,
// attenuation, latency, recovery, hysteresis, expander curve, vocal tails,
// sample-rate and block-size consistency, stereo link, bypass, learning.

#include "../support/DrumSignals.h"
#include "../support/TestSignals.h"

#include "mixcore/gain/SmartGainPresets.h"
#include "mixcore/gate/GateProfiles.h"
#include "mixcore/gate/NoiseGate.h"
#include "mixcore/strip/ChannelPresets.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>

using namespace mixcore;
using namespace mixcore::gate;
using namespace mixcore::test;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

std::unique_ptr<NoiseGate> makeGate(double sr, const GateSettings& s, int channels = 1)
{
    auto g = std::make_unique<NoiseGate>();
    g->prepare(sr, 4096, channels);
    g->setSettings(s);
    g->reset();
    return g;
}

Signal run(NoiseGate& g, const Signal& in, int block = 480, std::vector<GateTelemetry>* ticks = nullptr)
{
    std::vector<Signal> ch { in };
    std::vector<float*> ptr(1);
    for (size_t pos = 0; pos < in.size(); pos += static_cast<size_t>(block))
    {
        const int n = static_cast<int>(std::min(static_cast<size_t>(block), in.size() - pos));
        ptr[0] = ch[0].data() + pos;
        g.process(ptr.data(), 1, n);
        if (ticks)
        {
            GateTelemetry t;
            if (g.readTelemetry(t))
                ticks->push_back(t);
        }
    }
    return ch[0];
}

/// Per-sample gain in dB (output / input), skipping near-zero input.
double gainDbAt(const Signal& in, const Signal& out, size_t i, int latency = 0)
{
    const auto j = i - static_cast<size_t>(latency);
    return 20.0 * std::log10(std::abs(static_cast<double>(out[i])) / std::abs(static_cast<double>(in[j])));
}

/// RMS gain over [a, b) in dB.
double segmentGainDb(const Signal& in, const Signal& out, size_t a, size_t b, int latency = 0)
{
    return rmsDb(out, a + static_cast<size_t>(latency), b + static_cast<size_t>(latency)) - rmsDb(in, a, b);
}

GateSettings hardGate()
{
    GateSettings s;
    s.enabled = true;
    s.thresholdDb = -30.0f;
    s.rangeDb = 40.0f;
    s.ratio = 20.0f;
    s.hysteresisDb = 6.0f;
    s.attackMs = 1.0f;
    s.holdMs = 50.0f;
    s.releaseMs = 100.0f;
    s.rmsDetector = false; // fast peak detector, no key filter
    s.sidechainHpfHz = 20.0f;
    s.sidechainLpfHz = 20000.0f;
    return s;
}

/// Number of hits kickGroove() / the onset times it uses.
std::vector<size_t> kickOnsets(double sr, double seconds, double bpm)
{
    std::vector<size_t> on;
    const auto period = static_cast<size_t>(60.0 / bpm * sr);
    const auto total = static_cast<size_t>(seconds * sr);
    for (size_t start = static_cast<size_t>(0.25 * sr); start < total; start += period)
        on.push_back(start);
    return on;
}

Signal concat(std::initializer_list<Signal> parts)
{
    Signal out;
    for (const auto& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}
} // namespace

TEST_CASE("Gate settings validate and round-trip", "[gate][settings]")
{
    GateSettings s;
    s.thresholdDb = 10.0f;
    s.ratio = 0.5f;
    s.attackMs = std::numeric_limits<float>::quiet_NaN();
    s.lookaheadMs = 99.0f;
    s.validate();
    CHECK(s.thresholdDb == 0.0f);
    CHECK(s.ratio == 1.2f);
    CHECK(s.attackMs == GateSettings {}.attackMs);
    CHECK(s.lookaheadMs == 5.0f);

    auto t = hardGate();
    t.transientTrigger = true;
    t.rmsDetector = true;
    t.sidechainHpfHz = 150.0f;
    CHECK(GateSettings::deserialise(t.serialise()) == t);
    CHECK(GateSettings::deserialise("thresholdDb=-20;nonsense;futureKey=3;") .thresholdDb == -20.0f);
    CHECK_FALSE(GateSettings::deserialise("").enabled); // missing data -> safe default (off)
}

TEST_CASE("Closed gate attenuates by exactly the range; open gate is unity", "[gate][attenuation]")
{
    const float range = GENERATE(10.0f, 40.0f, 80.0f);
    auto s = hardGate();
    s.rangeDb = range;
    auto g = makeGate(kSr, s);

    // 1 s below the close threshold, then 1 s well above threshold.
    const auto quiet = sine(kSr, 1000.0, 1.0, static_cast<double>(dbToGain(-45.0f)));
    const auto loud = sine(kSr, 1000.0, 1.0, static_cast<double>(dbToGain(-12.0f)));
    const auto in = concat({ quiet, loud });
    const auto out = run(*g, in);

    INFO("range " << range);
    CHECK(segmentGainDb(in, out, 4800, 48000) == Approx(-static_cast<double>(range)).margin(0.05));
    CHECK(segmentGainDb(in, out, 48000 + 4800, 96000) == Approx(0.0).margin(1.0e-4));
}

TEST_CASE("Gate opens within the attack time and closes after hold + release", "[gate][timing]")
{
    auto s = hardGate(); // attack 1 ms, hold 50 ms, release 100 ms, range 40
    auto g = makeGate(kSr, s);
    const auto in = concat({ Signal(static_cast<size_t>(0.2 * kSr), 0.0f), sine(kSr, 1000.0, 0.5, static_cast<double>(dbToGain(-20.0f))),
                             Signal(static_cast<size_t>(1.0 * kSr), 0.0f) });
    // Track the gate gain with a constant probe: run a copy where silence is
    // replaced by a tiny DC offset so gain can be measured everywhere.
    Signal probe = in;
    for (auto& v : probe)
        if (v == 0.0f)
            v = 1.0e-4f; // -80 dBFS: far below the close threshold
    const auto out = run(*g, probe, 64);

    const size_t onset = static_cast<size_t>(0.2 * kSr);
    // Attack: fully open (within 0.1 dB) no later than 1 ms (+2 samples) after the onset.
    size_t openAt = onset;
    while (openAt < probe.size() && !(probe[openAt] != 0.0f && std::abs(probe[openAt]) > 1.0e-3f && gainDbAt(probe, out, openAt) > -0.1))
        ++openAt;
    // Find the first sample after which gain stays > -0.1 dB for 10 ms.
    size_t stableOpen = onset;
    for (size_t i = onset; i < onset + static_cast<size_t>(0.01 * kSr); ++i)
        if (std::abs(probe[i]) > 1.0e-3f && gainDbAt(probe, out, i) < -0.1)
            stableOpen = i;
    INFO("fully open after " << (stableOpen - onset) << " samples");
    CHECK(stableOpen - onset <= static_cast<size_t>(0.001 * kSr) + 2);

    // Release: the phrase ends at 0.7 s. The detector falls below the close
    // threshold within ~16 ms (10 ms peak release over 16 dB); then hold 50 ms;
    // then a 100 ms ramp to -40 dB.
    const size_t end = static_cast<size_t>(0.7 * kSr);
    auto gainAfter = [&](double ms) { return gainDbAt(probe, out, end + static_cast<size_t>(ms * 0.001 * kSr)); };
    CHECK(gainAfter(50.0) == Approx(0.0).margin(0.01));      // still held open: tails protected
    CHECK(gainAfter(16.0 + 50.0 + 100.0 + 5.0) <= -39.9);    // fully closed by detector + hold + release
    CHECK(gainAfter(16.0 + 50.0 + 50.0) == Approx(-20.0).margin(2.5)); // linear-in-dB ramp halfway
}

TEST_CASE("Hysteresis prevents chatter around the threshold", "[gate][hysteresis]")
{
    // A tone whose level wobbles +/-2 dB around the threshold at 8 Hz.
    Signal in(static_cast<size_t>(3.0 * kSr));
    for (size_t i = 0; i < in.size(); ++i)
    {
        const double t = static_cast<double>(i) / kSr;
        const double levelDb = -30.0 + 2.0 * std::sin(2.0 * kPi * 8.0 * t);
        in[i] = static_cast<float>(std::pow(10.0, levelDb / 20.0) * std::sin(2.0 * kPi * 500.0 * t));
    }
    auto withHyst = hardGate();
    withHyst.holdMs = 0.0f;
    std::vector<GateTelemetry> ticks;
    auto g = makeGate(kSr, withHyst);
    run(*g, in, 480, &ticks);
    CHECK(ticks.back().opensTotal == 1); // opened once and stayed open

    auto noHyst = withHyst;
    noHyst.hysteresisDb = 0.0f;
    ticks.clear();
    auto g2 = makeGate(kSr, noHyst);
    run(*g2, in, 480, &ticks);
    INFO("opens without hysteresis: " << ticks.back().opensTotal);
    CHECK(ticks.back().opensTotal >= 10); // documents why hysteresis matters
    CHECK(ticks.back().opensLastSecond >= 4);
}

TEST_CASE("Expander follows its static curve and is capped by the range", "[gate][expander]")
{
    GateSettings s;
    s.enabled = true;
    s.rmsDetector = false;
    s.sidechainHpfHz = 20.0f;
    s.sidechainLpfHz = 20000.0f;
    s.thresholdDb = -40.0f;
    s.ratio = 2.0f;
    s.rangeDb = 30.0f;
    s.hysteresisDb = 0.0f;
    s.holdMs = 0.0f;
    s.releaseMs = 5.0f;
    s.attackMs = 0.05f;
    // Below threshold by d dB -> attenuation d * (ratio - 1), capped at range.
    for (const float level : { -45.0f, -50.0f, -55.0f, -60.0f, -80.0f })
    {
        auto g = makeGate(kSr, s);
        const auto in = sine(kSr, 1000.0, 1.0, static_cast<double>(dbToGain(level)));
        const auto out = run(*g, in);
        const double expected = -std::min(30.0, static_cast<double>(-40.0f - level) * 1.0);
        INFO("level " << level);
        CHECK(segmentGainDb(in, out, 9600, 48000) == Approx(expected).margin(0.5));
    }
}

TEST_CASE("Kick gate opens on every kick and never on snare bleed", "[gate][drums][false-trigger]")
{
    // Kick mic at 60 BPM: kick hits (peak 0.4 = -8 dBFS) plus snare bleed about
    // 20 dB lower (peak 0.04), placed half-way between kicks when the kick has
    // rung out, so any bleed open is visible as an extra open.
    const double bpm = 60.0;
    const double seconds = 8.25;
    const auto kick = kickGroove(kSr, seconds, bpm, 0.4f, -70.0, 5);
    const auto snareBleed = snareGroove(kSr, seconds, bpm, 0.04f, -90.0, 6);
    const auto offset = static_cast<size_t>(0.5 * 60.0 / bpm * kSr);
    Signal in(kick.size());
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = kick[i] + (i >= offset ? snareBleed[i - offset] : 0.0f);
    const auto onsets = kickOnsets(kSr, seconds, bpm);

    SECTION("with the Kick profile (side-chain 30-150 Hz, transient trigger)")
    {
        auto s = findGateProfile("Kick")->settings;
        std::vector<GateTelemetry> ticks;
        auto g = makeGate(kSr, s);
        const auto out = run(*g, in, 480, &ticks);
        INFO("opens " << ticks.back().opensTotal << " kick hits " << onsets.size());
        CHECK(ticks.back().opensTotal == onsets.size()); // one open per kick, none for bleed
        // Every kick passes intact: first 5 ms of each hit within 0.5 dB.
        const int L = g->latencySamples();
        for (auto on : onsets)
            CHECK(segmentGainDb(in, out, on, on + 240, L) > -0.5);
    }

    SECTION("the kick key filter lowers snare bleed in the detector")
    {
        // Bleed alone through the Kick detector, with and without its key filter.
        auto peakDetector = [&](bool filtered) {
            auto s = findGateProfile("Kick")->settings;
            if (!filtered)
            {
                s.sidechainHpfHz = 20.0f;
                s.sidechainLpfHz = 20000.0f;
            }
            const auto bleedOnly = snareGroove(kSr, seconds, bpm, 0.04f, -90.0, 6);
            std::vector<GateTelemetry> ticks;
            auto g = makeGate(kSr, s);
            run(*g, bleedOnly, 480, &ticks);
            float mx = -200.0f;
            for (const auto& t : ticks)
                mx = std::max(mx, t.detectorDb);
            return mx;
        };
        const float unfiltered = peakDetector(false);
        const float filtered = peakDetector(true);
        INFO("snare bleed in kick detector: unfiltered " << unfiltered << " dBFS, keyed " << filtered << " dBFS");
        CHECK(filtered <= unfiltered - 3.5f);
        // And the profile threshold keeps a safety margin above keyed bleed.
        CHECK(findGateProfile("Kick")->settings.thresholdDb >= filtered + 4.0f);
    }
}

TEST_CASE("Transient trigger ignores sustained wash but opens on hits", "[gate][transient]")
{
    // Cymbal-like wash that swells in over 300 ms to -15 dBFS peak, then a snare hit on top.
    Rng rng(9);
    Signal in(static_cast<size_t>(2.0 * kSr), 0.0f);
    for (size_t i = 0; i < in.size(); ++i)
    {
        const double t = static_cast<double>(i) / kSr;
        const double swell = std::min(1.0, t / 0.3);
        in[i] = static_cast<float>(0.12 * swell * static_cast<double>(rng.bipolar()));
    }
    const auto hitAt = static_cast<size_t>(1.2 * kSr);
    for (size_t k = 0; k < static_cast<size_t>(0.2 * kSr); ++k)
    {
        const double t = static_cast<double>(k) / kSr;
        in[hitAt + k] += static_cast<float>(0.7 * std::exp(-t / 0.06) * std::sin(2.0 * kPi * 200.0 * t));
    }

    auto s = hardGate();
    s.thresholdDb = -24.0f;
    s.holdMs = 20.0f;
    s.releaseMs = 50.0f;

    s.transientTrigger = true;
    s.transientDb = 6.0f;
    std::vector<GateTelemetry> ticks;
    auto g = makeGate(kSr, s);
    run(*g, in, 480, &ticks);
    CHECK(ticks.at(100).state == GateState::Closed); // 1.0 s: wash alone stays out
    CHECK(ticks.back().opensTotal == 1);              // the hit opened it

    s.transientTrigger = false;
    ticks.clear();
    auto g2 = makeGate(kSr, s);
    run(*g2, in, 480, &ticks);
    CHECK(ticks.at(100).state != GateState::Closed); // without it, the wash holds the gate open
}

TEST_CASE("Lookahead preserves the attack of a hit and reports exact latency", "[gate][latency]")
{
    // A hit that starts at full level instantly.
    Signal in(static_cast<size_t>(0.5 * kSr), 0.0f);
    const auto at = static_cast<size_t>(0.2 * kSr);
    for (size_t k = 0; k < static_cast<size_t>(0.1 * kSr); ++k)
    {
        const double t = static_cast<double>(k) / kSr;
        in[at + k] = static_cast<float>(0.5 * std::exp(-t / 0.03) * std::sin(2.0 * kPi * 120.0 * t + 1.0));
    }
    auto s = hardGate();
    s.attackMs = 1.0f;

    auto onsetLossDb = [&](float lookaheadMs, int& latencyOut) {
        auto ls = s;
        ls.lookaheadMs = lookaheadMs;
        auto g = makeGate(kSr, ls);
        latencyOut = g->latencySamples();
        const auto out = run(*g, in, 128);
        const auto first = static_cast<size_t>(0.001 * kSr); // first 1 ms of the hit
        return rmsDb(out, at + static_cast<size_t>(latencyOut), at + static_cast<size_t>(latencyOut) + first)
               - rmsDb(in, at, at + first);
    };

    int latency = -1;
    const double lossNoLookahead = onsetLossDb(0.0f, latency);
    CHECK(latency == 0);
    const double lossLookahead = onsetLossDb(1.5f, latency);
    CHECK(latency == static_cast<int>(std::lround(0.0015 * kSr)));
    INFO("onset loss without lookahead " << lossNoLookahead << " dB, with " << lossLookahead << " dB");
    CHECK(lossNoLookahead < -3.0);            // a 1 ms attack eats the first millisecond
    CHECK(lossLookahead == Approx(0.0).margin(0.05)); // with lookahead the hit arrives at a fully open gate
}

TEST_CASE("Vocal gentle expander keeps phrases and tails, lowers noise between them", "[gate][vocal]")
{
    // Phrases with a 300 ms exponential "reverb" tail, over -60 dBFS room noise.
    Signal in = compose(kSr, { { Segment::Silence, 0.5 }, { Segment::Vocal, 2.0, -18.0 }, { Segment::Silence, 1.5 },
                               { Segment::Vocal, 2.0, -24.0 }, { Segment::Silence, 1.5 } },
                        -150.0);
    for (const double endT : { 2.5, 6.0 })
    {
        const auto end = static_cast<size_t>(endT * kSr);
        const double tailStart = std::abs(static_cast<double>(in[end - 2000])) + 0.02;
        for (size_t k = 0; k < static_cast<size_t>(0.6 * kSr); ++k)
        {
            const double t = static_cast<double>(k) / kSr;
            in[end + k] += static_cast<float>(tailStart * std::exp(-t / 0.08) * std::sin(2.0 * kPi * 330.0 * t));
        }
    }
    const auto room = noise(kSr, static_cast<double>(in.size()) / kSr + 0.01, -60.0, 17);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] += room[i];

    auto g = makeGate(kSr, findGateProfile("Vocal - Gentle Expander")->settings);
    const auto out = run(*g, in);

    auto seg = [&](double a, double b) {
        return segmentGainDb(in, out, static_cast<size_t>(a * kSr), static_cast<size_t>(b * kSr));
    };
    CHECK(seg(0.6, 2.45) == Approx(0.0).margin(0.1));  // loud phrase untouched
    CHECK(seg(4.1, 5.95) == Approx(0.0).margin(0.1));  // quieter phrase untouched
    CHECK(seg(2.5, 2.7) > -1.0);                        // first 200 ms of each tail kept
    CHECK(seg(6.0, 6.2) > -1.0);
    INFO("noise between phrases: " << seg(3.3, 3.95) << " dB");
    CHECK(seg(3.3, 3.95) < -8.0); // room noise between phrases lowered (range 10 dB)
    CHECK(seg(3.3, 3.95) >= -10.05);
}

TEST_CASE("Gate timing is the same at every sample rate", "[gate][samplerate]")
{
    std::vector<double> closeTimesMs;
    for (const double sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        auto s = hardGate();
        auto g = makeGate(sr, s);
        Signal in = concat({ sine(sr, 1000.0, 0.5, static_cast<double>(dbToGain(-20.0f))), Signal(static_cast<size_t>(0.5 * sr), 1.0e-4f) });
        const auto out = run(*g, in, 256);
        const auto end = static_cast<size_t>(0.5 * sr);
        size_t i = end;
        while (i < out.size() && gainDbAt(in, out, i) > -39.9)
            ++i;
        closeTimesMs.push_back(static_cast<double>(i - end) / sr * 1000.0);
    }
    for (double t : closeTimesMs)
        CHECK(t == Approx(closeTimesMs[1]).margin(1.0));
}

TEST_CASE("Gate output is identical for any block size", "[gate][blocksize]")
{
    auto s = findGateProfile("Snare")->settings; // includes lookahead and transient trigger
    const auto in = snareGroove(kSr, 4.0, 120.0, 0.4f, -50.0, 3);
    Signal reference;
    for (int block : { 4096, 1, 31, 480, 1000 })
    {
        auto g = makeGate(kSr, s);
        const auto out = run(*g, in, block);
        if (reference.empty())
            reference = out;
        else
            REQUIRE(out == reference);
    }
}

TEST_CASE("Stereo channels get identical gate gain", "[gate][stereo]")
{
    auto g = makeGate(kSr, hardGate(), 2);
    auto left = kickGroove(kSr, 3.0, 120.0, 0.4f, -60.0, 1);
    auto right = left;
    for (size_t i = 0; i < right.size(); ++i)
        right[i] *= (i % 3 == 0) ? -0.3f : 0.6f;
    const auto l0 = left, r0 = right;
    float* ptr[2] = { left.data(), right.data() };
    g->process(ptr, 2, static_cast<int>(left.size()));
    for (size_t i = 0; i < left.size(); ++i)
        if (std::abs(l0[i]) > 1.0e-3f && std::abs(r0[i]) > 1.0e-3f)
            REQUIRE(left[i] / l0[i] == Approx(right[i] / r0[i]).epsilon(1.0e-5));
}

TEST_CASE("Disabled or bypassed gate passes audio delayed by its latency", "[gate][bypass]")
{
    const auto in = kickGroove(kSr, 2.0, 120.0, 0.4f, -50.0, 2);

    auto s = findGateProfile("Kick")->settings;
    s.enabled = false;
    auto g = makeGate(kSr, s);
    const auto L = static_cast<size_t>(g->latencySamples());
    REQUIRE(L == static_cast<size_t>(std::lround(0.0015 * kSr)));
    const auto out = run(*g, in);
    for (size_t i = L; i < in.size(); ++i)
        REQUIRE(out[i] == in[i - L]);

    s.enabled = true;
    auto g2 = makeGate(kSr, s);
    g2->setBypassed(true);
    g2->reset();
    const auto out2 = run(*g2, in);
    for (size_t i = L; i < in.size(); ++i)
        REQUIRE(out2[i] == in[i - L]);
}

TEST_CASE("Learn threshold separates source from bleed", "[gate][learn]")
{
    SECTION("analysis on bimodal levels")
    {
        std::vector<float> levels;
        Rng rng(4);
        for (int i = 0; i < 700; ++i)
            levels.push_back(-48.0f + 2.0f * rng.bipolar()); // bleed
        for (int i = 0; i < 150; ++i)
            levels.push_back(-12.0f + 3.0f * rng.bipolar()); // hits
        const auto r = GateLearner::analyse(levels, false);
        INFO(r.summary);
        CHECK(r.valid);
        CHECK(r.confidence == LearnResult::Confidence::Good);
        CHECK(r.bleedDb == Approx(-48.0f).margin(1.0));
        CHECK(r.sourceDb == Approx(-12.0f).margin(1.5));
        CHECK(r.suggestedThresholdDb == Approx(-48.0f + 0.4f * 36.0f).margin(1.5));
    }

    SECTION("no usable separation is reported honestly")
    {
        std::vector<float> levels;
        Rng rng(5);
        for (int i = 0; i < 800; ++i)
            levels.push_back(-30.0f + 3.0f * rng.bipolar());
        const auto r = GateLearner::analyse(levels, false);
        CHECK_FALSE(r.valid);
        CHECK(r.confidence == LearnResult::Confidence::None);
        CHECK(r.summary.find("Reposition") != std::string::npos);
    }

    SECTION("silence is not mistaken for bleed")
    {
        const auto r = GateLearner::analyse(std::vector<float>(800, -150.0f), true);
        CHECK_FALSE(r.valid);
    }

    SECTION("percussive sources get a threshold relative to the hit peaks")
    {
        std::vector<float> levels;
        Rng rng(6);
        for (int i = 0; i < 600; ++i)
            levels.push_back(-70.0f + 2.0f * rng.bipolar()); // very quiet (filtered) bleed
        for (int i = 0; i < 300; ++i)
            levels.push_back(-8.0f - 30.0f * 0.5f * (rng.bipolar() + 1.0f)); // hit peaks and their decay
        const auto sustained = GateLearner::analyse(levels, false);
        const auto percussive = GateLearner::analyse(levels, true);
        INFO(percussive.summary);
        CHECK(percussive.suggestedThresholdDb > sustained.suggestedThresholdDb + 5.0f);
        CHECK(percussive.suggestedThresholdDb == Approx(percussive.sourceDb - 18.0f).margin(0.01));
    }

    SECTION("end to end on a kick groove with snare bleed")
    {
        const double bpm = 60.0, seconds = 8.25;
        const auto kick = kickGroove(kSr, seconds, bpm, 0.4f, -60.0, 7);
        const auto bleed = snareGroove(kSr, seconds, bpm, 0.04f, -90.0, 8);
        const auto offset = static_cast<size_t>(0.5 * 60.0 / bpm * kSr); // bleed between kicks
        Signal in(kick.size());
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = kick[i] + (i >= offset ? bleed[i - offset] : 0.0f);
        auto s = findGateProfile("Kick")->settings;
        s.thresholdDb = -70.0f; // deliberately wrong starting point: learning must fix it
        auto g = makeGate(kSr, s);
        g->learner().requestStart(6.0);
        run(*g, in);
        REQUIRE(g->learner().state() == LearnState::Ready);
        const auto r = g->learner().computeResult(true);
        INFO(r.summary);
        CHECK(r.valid);
        CHECK(r.suggestedThresholdDb > r.bleedDb + 5.0f);
        CHECK(r.suggestedThresholdDb < r.sourceDb - 5.0f);

        // Applying the suggestion: every kick opens the gate (and passes
        // intact), the bleed between kicks never does.
        s.thresholdDb = r.suggestedThresholdDb;
        s.hysteresisDb = r.suggestedHysteresisDb;
        std::vector<GateTelemetry> ticks;
        auto g2 = makeGate(kSr, s);
        const auto out = run(*g2, in, 480, &ticks);
        const auto onsets = kickOnsets(kSr, seconds, bpm);
        CHECK(ticks.back().opensTotal == onsets.size());
        for (auto on : onsets)
            CHECK(segmentGainDb(in, out, on, on + 240, g2->latencySamples()) > -0.5);
    }
}

TEST_CASE("Gate survives random settings and hostile input", "[gate][stress]")
{
    Rng rng(77);
    auto uni = [&](float lo, float hi) { return lo + (hi - lo) * 0.5f * (rng.bipolar() + 1.0f); };
    auto g = makeGate(kSr, GateSettings {}, 2);
    std::vector<float> l(2048), r(2048);
    float* ptr[2] = { l.data(), r.data() };
    size_t bad = 0;
    for (int block = 0; block < 1500; ++block)
    {
        GateSettings s;
        s.enabled = rng.bipolar() > -0.5f;
        s.thresholdDb = uni(-100.0f, 10.0f);
        s.rangeDb = uni(-10.0f, 100.0f);
        s.ratio = uni(0.0f, 40.0f);
        s.attackMs = uni(-1.0f, 60.0f);
        s.holdMs = uni(0.0f, 3000.0f);
        s.releaseMs = uni(0.0f, 5000.0f);
        s.sidechainHpfHz = uni(0.0f, 3000.0f);
        s.sidechainLpfHz = uni(100.0f, 30000.0f);
        s.rmsDetector = rng.bipolar() > 0.0f;
        s.transientTrigger = rng.bipolar() > 0.0f;
        s.lookaheadMs = uni(-1.0f, 8.0f);
        g->setSettings(s);
        g->setBypassed(rng.bipolar() > 0.8f);
        const int n = 1 + static_cast<int>((rng.next() >> 33) % 2048);
        const float level = uni(0.0f, 2.0f);
        for (int i = 0; i < n; ++i)
        {
            l[static_cast<size_t>(i)] = level * rng.bipolar();
            r[static_cast<size_t>(i)] = level * rng.bipolar();
        }
        if (rng.bipolar() > 0.9f)
            l[0] = std::numeric_limits<float>::infinity();
        g->process(ptr, 2, n);
        for (int i = 0; i < n; ++i)
        {
            bad += std::isfinite(l[static_cast<size_t>(i)]) ? 0u : 1u;
            bad += std::abs(r[static_cast<size_t>(i)]) <= 2.0f * 1.0001f ? 0u : 1u; // a gate never adds gain
        }
    }
    CHECK(bad == 0);
}

// --------------------------------------------------------------- Channel strip

TEST_CASE("Channel presets combine Smart Gain and gate profiles", "[strip][presets]")
{
    using namespace mixcore::strip;
    const auto& presets = channelPresets();
    REQUIRE(presets.size() == gain::factoryPresets().size());
    for (const auto& p : presets)
    {
        INFO(p.name);
        CHECK_FALSE(p.gateProfile.empty());
        CHECK(gate::findGateProfile(p.gateProfile) != nullptr);
        const bool isDrum = p.category == "Drums";
        CHECK(p.settings.gate.enabled == isDrum); // drums: gate on; voices: profile loaded, gate off
    }
    CHECK(findChannelPreset("Kick Drum")->gateProfile == "Kick");
    CHECK(findChannelPreset("Lead Vocal - Natural")->gateProfile == "Vocal - Gentle Expander");
    CHECK(findChannelPreset("Backing Vocal - Airy")->gateProfile == "Backing Vocal");

    ChannelSettings session;
    session.gain.staticGainDb = 5.0f;
    session.gain.bypass = true;
    const auto applied = applyChannelPreset(session, findChannelPreset("Snare Drum")->settings);
    CHECK(applied.gain.staticGainDb == 5.0f);
    CHECK(applied.gain.bypass);
    CHECK(applied.gate.enabled);
    CHECK(matchesChannelPreset(applied, findChannelPreset("Snare Drum")->settings));
    auto edited = applied;
    edited.gate.thresholdDb -= 3.0f;
    CHECK_FALSE(matchesChannelPreset(edited, findChannelPreset("Snare Drum")->settings));
}

TEST_CASE("Channel strip sums latency and bypasses as a delayed copy", "[strip][latency][bypass]")
{
    using namespace mixcore::strip;
    ChannelSettings s = findChannelPreset("Kick Drum")->settings; // gate lookahead 1.5 ms
    s.gain.peakGuardEnabled = true;                                 // +2 ms
    ChannelStrip strip;
    strip.prepare(kSr, 512, 1);
    strip.setSettings(s);
    strip.reset();
    const int expected = static_cast<int>(std::lround(0.0015 * kSr)) + static_cast<int>(std::lround(0.002 * kSr));
    CHECK(strip.latencySamples() == expected);

    s.gain.bypass = true;
    strip.setSettings(s);
    strip.reset();
    auto in = kickGroove(kSr, 2.0, 120.0, 0.5f, -50.0, 3);
    auto buf = in;
    float* p[1] = { buf.data() };
    for (size_t pos = 0; pos < buf.size(); pos += 256)
    {
        p[0] = buf.data() + pos;
        strip.process(p, 1, static_cast<int>(std::min<size_t>(256, buf.size() - pos)));
    }
    const auto L = static_cast<size_t>(expected);
    for (size_t i = L; i < in.size(); ++i)
        REQUIRE(buf[i] == in[i - L]);
}

#include "mixcore/strip/StripCoach.h"

TEST_CASE("Mix Coach explains gate problems", "[strip][coach]")
{
    using namespace mixcore::strip;
    ChannelSettings s;
    s.gate = findGateProfile("Vocal - Gentle Expander")->settings;
    gain::SmartGainTelemetry gt;
    gt.activity = gain::ActivityState::Active;
    gate::GateTelemetry t;
    t.enabled = true;
    t.state = GateState::Closed;
    t.closedSeconds = 4.0f;
    t.opensLastSecond = 9;
    t.hardGate = false;
    const auto advice = evaluateStripAdvice(gt, t, s);
    auto has = [&](const char* id) {
        return std::any_of(advice.begin(), advice.end(), [&](const gain::Advice& a) { return a.id == id; });
    };
    CHECK(has("gate-chatter"));
    CHECK(has("gate-stuck-closed"));
    CHECK_FALSE(has("gate-vocal-hard"));
    for (const auto& a : advice)
    {
        CHECK_FALSE(a.whyItMatters.empty());
        CHECK_FALSE(a.recommendation.empty());
    }

    t.enabled = false; // a disabled gate never produces gate advice
    const auto none = evaluateStripAdvice(gt, t, s);
    CHECK(std::none_of(none.begin(), none.end(), [](const gain::Advice& a) { return a.id.rfind("gate-", 0) == 0; }));
}

TEST_CASE("Default gate settings equal the gentle vocal profile, switched off", "[gate][settings]")
{
    auto profile = findGateProfile("Vocal - Gentle Expander")->settings;
    profile.enabled = false;
    CHECK(GateSettings {} == profile);
    CHECK(strip::findChannelPreset("Lead Vocal - Natural")->settings.gate == GateSettings {});
}
