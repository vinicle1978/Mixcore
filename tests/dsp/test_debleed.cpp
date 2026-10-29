// De-bleed tests. Synthetic snare + cymbal signals: deterministic, but not a
// substitute for listening tests on real multitrack drum recordings.

#include "../support/DrumSignals.h"

#include "mixcore/dsp/Biquad.h"
#include "mixcore/drum/DeBleed.h"
#include "mixcore/drum/DrumProfiles.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace mixcore;
using namespace mixcore::drum;
using namespace mixcore::test;

namespace {

constexpr double kSr = 48000.0;
constexpr double kBpm = 120.0; // snare on beats 2 and 4 -> one hit per second

/// Bright cymbal-like wash: noise high-passed at `hpHz` (8th order).
Signal cymbal(double sr, double seconds, double rmsDbValue, std::uint64_t seed, double hpHz = 7000.0)
{
    Signal s = noise(sr, seconds, 0.0, seed);
    dsp::Biquad f[4];
    for (auto& b : f)
        b.setHighPass(sr, hpHz, 0.7071);
    for (auto& x : s)
        for (auto& b : f)
            x = b.process(x);
    scaleToRmsDb(s, rmsDbValue);
    return s;
}

/// Energy (dB) above `hz` in [begin, end) of `s`, measured with an 8th-order high-pass.
double highBandDb(const Signal& s, size_t begin, size_t end, double hz = 6500.0, double sr = kSr)
{
    dsp::Biquad f[4];
    for (auto& b : f)
        b.setHighPass(sr, hz, 0.7071);
    double e = 0.0;
    for (size_t i = 0; i < s.size() && i < end; ++i)
    {
        float y = s[i];
        for (auto& b : f)
            y = b.process(y);
        if (i >= begin)
            e += static_cast<double>(y) * static_cast<double>(y);
    }
    return 10.0 * std::log10(std::max(e / static_cast<double>(end - begin), 1e-30));
}

double lowBandDb(const Signal& s, size_t begin, size_t end, double hz = 1000.0, double sr = kSr)
{
    dsp::Biquad f[4];
    for (auto& b : f)
        b.setLowPass(sr, hz, 0.7071);
    double e = 0.0;
    for (size_t i = 0; i < s.size() && i < end; ++i)
    {
        float y = s[i];
        for (auto& b : f)
            y = b.process(y);
        if (i >= begin)
            e += static_cast<double>(y) * static_cast<double>(y);
    }
    return 10.0 * std::log10(std::max(e / static_cast<double>(end - begin), 1e-30));
}

Signal mixSignals(const Signal& a, const Signal& b)
{
    Signal out(std::min(a.size(), b.size()));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = a[i] + b[i];
    return out;
}

Signal run(DeBleed& d, const Signal& in, int block = 256)
{
    Signal out = in;
    for (size_t pos = 0; pos < out.size(); pos += static_cast<size_t>(block))
    {
        const int n = static_cast<int>(std::min(out.size() - pos, static_cast<size_t>(block)));
        float* ch[1] = { out.data() + pos };
        d.process(ch, 1, n);
    }
    return out;
}

void setup(DeBleed& d, const DrumSettings& s, double sr = kSr, int channels = 1)
{
    REQUIRE(d.prepare(sr, 512, channels));
    d.setSettings(s);
    d.reset();
}

DrumSettings snareProfile()
{
    const auto* p = findDrumProfile("snare");
    REQUIRE(p != nullptr);
    return p->settings;
}

/// First sample of every snare hit produced by snareGroove(sr, ., kBpm, ...).
std::vector<size_t> snareHits(double sr, size_t length)
{
    std::vector<size_t> hits;
    const auto bar = static_cast<size_t>(4.0 * 60.0 / kBpm * sr);
    const auto beat = bar / 4;
    for (size_t barStart = static_cast<size_t>(0.25 * sr); barStart < length; barStart += bar)
        for (size_t h : { barStart + beat, barStart + 3 * beat })
            if (h < length)
                hits.push_back(h);
    return hits;
}

} // namespace

TEST_CASE("De-bleed settings validate and round-trip", "[drum][debleed]")
{
    DrumSettings s;
    s.debleedAmount = 500.0f;
    s.debleedCrossoverHz = std::numeric_limits<float>::quiet_NaN();
    s.debleedLengthMs = 1.0f;
    s.validate();
    CHECK(s.debleedAmount == 100.0f);
    CHECK(s.debleedCrossoverHz == DrumSettings {}.debleedCrossoverHz);
    CHECK(s.debleedLengthMs == 10.0f);

    DrumSettings t = snareProfile();
    t.debleedToleranceDb = 4.5f;
    CHECK(DrumSettings::deserialise(t.serialise()) == t);
    CHECK(DrumSettings::deserialise("garbage;;=;x=1") == DrumSettings {});
    CHECK_FALSE(DrumSettings {}.debleedEnabled);
}

TEST_CASE("Every drum profile is valid", "[drum][debleed]")
{
    for (const auto& p : drumProfiles())
    {
        DrumSettings v = p.settings;
        v.validate();
        CHECK(v == p.settings);
    }
    CHECK(findDrumProfile("nope") == nullptr);
}

TEST_CASE("De-bleed adds no latency and is bit-exact when disabled", "[drum][debleed]")
{
    DrumSettings s = snareProfile();
    s.debleedEnabled = false;
    DeBleed d;
    setup(d, s);
    CHECK(d.latencySamples() == 0);
    const auto in = mixSignals(snareGroove(kSr, 3.0, kBpm, 0.5f, -50.0, 3), cymbal(kSr, 3.0, -24.0, 9));
    const auto out = run(d, in);
    CHECK(out == in);

    // Bypass also passes through bit-exact (after the 10 ms fade completes).
    DeBleed e;
    setup(e, snareProfile());
    e.setBypassed(true);
    e.reset();
    CHECK(run(e, in) == in);
}

TEST_CASE("De-bleed reduces cymbal wash between snare hits", "[drum][debleed]")
{
    const double seconds = 8.0;
    const auto snare = snareGroove(kSr, seconds, kBpm, 0.5f, -70.0, 4);
    const auto wash = cymbal(kSr, seconds, -30.0, 11);
    const auto in = mixSignals(snare, wash);
    DeBleed d;
    setup(d, snareProfile());
    const auto out = run(d, in);

    const auto hits = snareHits(kSr, in.size());
    double worst = 1e9;
    int windows = 0;
    for (size_t h : hits)
    {
        const size_t b = h + static_cast<size_t>(0.6 * kSr);
        const size_t e = h + static_cast<size_t>(0.95 * kSr);
        if (h < static_cast<size_t>(2.0 * kSr) || e > in.size())
            continue;
        worst = std::min(worst, highBandDb(in, b, e) - highBandDb(out, b, e));
        ++windows;
    }
    REQUIRE(windows >= 4);
    INFO("worst between-hit reduction " << worst << " dB");
    CHECK(worst >= 12.0);

    DeBleedTelemetry t;
    REQUIRE(d.readTelemetry(t));
    CHECK(t.enabled);
    CHECK(t.hitsSeen >= 6);
}

TEST_CASE("De-bleed is transparent on a clean snare", "[drum][debleed]")
{
    const double seconds = 8.0;
    const auto in = snareGroove(kSr, seconds, kBpm, 0.5f, -70.0, 5);
    DeBleed d;
    setup(d, snareProfile());
    const auto out = run(d, in);
    for (size_t h : snareHits(kSr, in.size()))
    {
        const size_t e = h + static_cast<size_t>(0.12 * kSr);
        if (e > in.size())
            continue;
        const double hfLoss = highBandDb(in, h, e) - highBandDb(out, h, e);
        const double lfDiff = std::abs(lowBandDb(in, h, e) - lowBandDb(out, h, e));
        INFO("hit at " << h << ": HF loss " << hfLoss << " dB, LF diff " << lfDiff << " dB");
        CHECK(hfLoss <= 1.5);
        CHECK(lfDiff <= 0.1);
    }
}

TEST_CASE("De-bleed reduces a crash ringing over a snare hit", "[drum][debleed]")
{
    // A crash starts after 4.4 s (the drum has been learned) and rings over
    // the following hits at roughly the level of the snare's own high end.
    // Best possible = remove the crash completely and keep only the snare's
    // own high end; Length and Tolerance trade how close MixCore gets to that
    // against how much of a long-ringing drum's own top is kept.
    const double seconds = 8.0;
    const auto snare = snareGroove(kSr, seconds, kBpm, 0.5f, -70.0, 6);
    auto crash = cymbal(kSr, seconds, -16.0, 13);
    for (size_t i = 0; i < static_cast<size_t>(4.4 * kSr); ++i)
        crash[i] = 0.0f;
    const auto in = mixSignals(snare, crash);

    struct Case
    {
        const char* name;
        float lengthMs;
        float toleranceDb;
        double fractionOfIdeal;
    };
    for (const Case c : { Case { "snare profile", 150.0f, 3.0f, 0.3 }, Case { "tight", 60.0f, 0.0f, 0.65 } })
    {
        DrumSettings s = snareProfile();
        s.debleedLengthMs = c.lengthMs;
        s.debleedToleranceDb = c.toleranceDb;
        DeBleed d;
        setup(d, s);
        const auto out = run(d, in);

        int checked = 0;
        for (size_t h : snareHits(kSr, in.size()))
        {
            // The ringing tail (30-150 ms), where a gate would be fully open.
            const size_t b = h + static_cast<size_t>(0.03 * kSr);
            const size_t e = h + static_cast<size_t>(0.15 * kSr);
            if (h < static_cast<size_t>(4.6 * kSr) || e > in.size())
                continue;
            const double red = highBandDb(in, b, e) - highBandDb(out, b, e);
            const double ideal = highBandDb(in, b, e) - highBandDb(snare, b, e);
            const double attackRed = highBandDb(in, h, b) - highBandDb(out, h, b);
            INFO(c.name << ", hit at " << h << ": tail reduction " << red << " dB of an ideal " << ideal
                        << " dB; attack " << attackRed << " dB");
            CHECK(ideal >= 6.0);
            CHECK(red >= c.fractionOfIdeal * ideal);
            CHECK(red <= ideal + 1.0); // never removes more than the crash
            CHECK(attackRed >= 0.0);
            ++checked;
        }
        CHECK(checked >= 2);

        // ... and the same settings stay transparent on the clean snare.
        DeBleed clean;
        setup(clean, s);
        const auto cleanOut = run(clean, snare);
        for (size_t h : snareHits(kSr, snare.size()))
        {
            const size_t e = h + static_cast<size_t>(0.15 * kSr);
            if (e > snare.size())
                continue;
            INFO(c.name << " clean hit at " << h);
            CHECK(highBandDb(snare, h, e) - highBandDb(cleanOut, h, e) <= 1.5);
        }
    }
}

TEST_CASE("Ratio learning is robust to crashes on some hits", "[drum][debleed]")
{
    const double seconds = 12.0;
    const auto snare = snareGroove(kSr, seconds, kBpm, 0.5f, -70.0, 7);
    DeBleed clean;
    setup(clean, snareProfile());
    run(clean, snare);
    DeBleedTelemetry tc;
    REQUIRE(clean.readTelemetry(tc));

    // A short loud crash on every other hit.
    Signal crashes(snare.size(), 0.0f);
    const auto burst = cymbal(kSr, 0.3, -10.0, 21);
    const auto hits = snareHits(kSr, snare.size());
    for (size_t i = 0; i < hits.size(); i += 2)
        for (size_t k = 0; k < burst.size() && hits[i] + k < crashes.size(); ++k)
            crashes[hits[i] + k] += static_cast<float>(static_cast<double>(burst[k]) * std::exp(-static_cast<double>(k) / (0.1 * kSr)));
    DeBleed dirty;
    setup(dirty, snareProfile());
    run(dirty, mixSignals(snare, crashes));
    DeBleedTelemetry td;
    REQUIRE(dirty.readTelemetry(td));
    INFO("clean ratio " << tc.learnedRatioDb << " dB, with crashes " << td.learnedRatioDb << " dB");
    CHECK(td.learnedRatioDb - tc.learnedRatioDb <= 2.0f);
    CHECK(tc.hitsSeen >= 10);
}

TEST_CASE("De-bleed output is independent of block size", "[drum][debleed]")
{
    const auto in = mixSignals(snareGroove(kSr, 4.0, kBpm, 0.5f, -60.0, 8), cymbal(kSr, 4.0, -28.0, 14));
    DeBleed a;
    setup(a, snareProfile());
    DeBleed b;
    setup(b, snareProfile());
    const auto oa = run(a, in, 64);
    const auto ob = run(b, in, 1031);
    CHECK(oa == ob);
}

TEST_CASE("De-bleed behaves consistently across sample rates", "[drum][debleed]")
{
    for (double sr : { 44100.0, 96000.0 })
    {
        const double seconds = 8.0;
        const auto in = mixSignals(snareGroove(sr, seconds, kBpm, 0.5f, -70.0, 4), cymbal(sr, seconds, -30.0, 11));
        DeBleed d;
        setup(d, snareProfile(), sr);
        const auto out = run(d, in);
        const size_t h = snareHits(sr, in.size())[4];
        const size_t b = h + static_cast<size_t>(0.6 * sr), e = h + static_cast<size_t>(0.95 * sr);
        const double red = highBandDb(in, b, e, 6500.0, sr) - highBandDb(out, b, e, 6500.0, sr);
        INFO("sr " << sr << " reduction " << red);
        CHECK(red >= 12.0);
    }
}

TEST_CASE("De-bleed links stereo channels and survives non-finite input", "[drum][debleed]")
{
    DeBleed d;
    setup(d, snareProfile(), kSr, 2);
    auto l = mixSignals(snareGroove(kSr, 3.0, kBpm, 0.5f, -60.0, 9), cymbal(kSr, 3.0, -26.0, 15));
    auto r = l;
    l[1000] = std::numeric_limits<float>::quiet_NaN();
    r[1000] = 0.0f;
    for (size_t pos = 0; pos < l.size(); pos += 256)
    {
        const int n = static_cast<int>(std::min<size_t>(256, l.size() - pos));
        float* ch[2] = { l.data() + pos, r.data() + pos };
        d.process(ch, 2, n);
    }
    for (size_t i = 0; i < l.size(); ++i)
    {
        REQUIRE(std::isfinite(l[i]));
        REQUIRE(l[i] == r[i]);
    }
}

TEST_CASE("Enabling de-bleed mid-stream fades in without a click", "[drum][debleed]")
{
    DrumSettings s = snareProfile();
    s.debleedEnabled = false;
    DeBleed d;
    setup(d, s);
    const auto in = mixSignals(snareGroove(kSr, 3.0, kBpm, 0.5f, -60.0, 10), cymbal(kSr, 3.0, -26.0, 16));
    Signal out = in;
    float maxStep = 0.0f, maxInStep = 0.0f;
    for (size_t pos = 0; pos < out.size(); pos += 128)
    {
        if (pos == static_cast<size_t>(1.5 * kSr) / 128 * 128)
        {
            s.debleedEnabled = true;
            d.setSettings(s);
        }
        const int n = static_cast<int>(std::min<size_t>(128, out.size() - pos));
        float* ch[1] = { out.data() + pos };
        d.process(ch, 1, n);
    }
    for (size_t i = 1; i < out.size(); ++i)
    {
        maxStep = std::max(maxStep, std::abs(out[i] - out[i - 1]));
        maxInStep = std::max(maxInStep, std::abs(in[i] - in[i - 1]));
    }
    CHECK(maxStep <= maxInStep * 1.25f);
}

TEST_CASE("Reset learning forgets the drum", "[drum][debleed]")
{
    DeBleed d;
    setup(d, snareProfile());
    run(d, snareGroove(kSr, 4.0, kBpm, 0.5f, -70.0, 12));
    DeBleedTelemetry t;
    REQUIRE(d.readTelemetry(t));
    CHECK(t.hitsSeen > 0);
    d.resetLearning();
    run(d, Signal(static_cast<size_t>(0.05 * kSr), 0.0f));
    REQUIRE(d.readTelemetry(t));
    CHECK(t.hitsSeen == 0);
    CHECK(t.learnedRatioDb == 10.0f); // back to permissive
}
