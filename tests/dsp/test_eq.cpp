// Parametric EQ, spectrum analysis and EQ Coach tests.

#include "../support/TestSignals.h"

#include "mixcore/eq/EqCoach.h"
#include "mixcore/eq/EqFilters.h"
#include "mixcore/eq/EqProfiles.h"
#include "mixcore/eq/ParametricEq.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace mixcore;
using namespace mixcore::eq;
using namespace mixcore::test;
using Catch::Approx;

namespace {

constexpr double kSr = 48000.0;

EqSettings enabledFlat()
{
    EqSettings s;
    s.enabled = true;
    return s;
}

void setup(ParametricEq& e, const EqSettings& s, double sr = kSr, int ch = 1)
{
    REQUIRE(e.prepare(sr, 512, ch));
    e.setSettings(s);
    e.reset();
}

Signal run(ParametricEq& e, const Signal& in, int block = 256)
{
    Signal out = in;
    for (size_t pos = 0; pos < out.size(); pos += static_cast<size_t>(block))
    {
        float* ch[1] = { out.data() + pos };
        e.process(ch, 1, static_cast<int>(std::min(out.size() - pos, static_cast<size_t>(block))));
    }
    return out;
}

/// Steady-state gain (dB) of the EQ for a sine at `freq`.
double measureDb(const EqSettings& s, double freq, double sr = kSr)
{
    ParametricEq e;
    setup(e, s, sr);
    const double seconds = std::max(0.5, 40.0 / freq);
    const auto in = sine(sr, freq, seconds, 0.25);
    const auto out = run(e, in);
    const auto from = out.size() / 2;
    return rmsDb(out, from) - rmsDb(in, from);
}

/// Pink-ish noise (Paul Kellet's filter), deterministic.
Signal pink(double sr, double seconds, std::uint64_t seed, float amp = 0.1f)
{
    Rng rng(seed);
    Signal s(static_cast<size_t>(seconds * sr));
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& v : s)
    {
        const double w = static_cast<double>(rng.bipolar());
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        v = static_cast<float>((b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11 * static_cast<double>(amp) * 10.0);
        b6 = w * 0.115926;
    }
    return s;
}

/// Applies one bell to a signal (to build test material with a known resonance).
Signal withBell(const Signal& in, double f, double q, double gainDb, double sr = kSr)
{
    const auto c = designPeak(sr, f, q, gainDb);
    Signal out(in.size());
    double z1 = 0, z2 = 0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        const double x = static_cast<double>(in[i]);
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        out[i] = static_cast<float>(y);
    }
    return out;
}

/// Applies any biquad.
Signal withCoefs(const Signal& in, const Coefs& c)
{
    Signal out(in.size());
    double z1 = 0, z2 = 0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        const double x = static_cast<double>(in[i]);
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        out[i] = static_cast<float>(y);
    }
    return out;
}

/// Noise shaped roughly like a long-term vocal spectrum (energy around
/// 200-500 Hz, falling above 1 kHz).
Signal vocalShaped(std::uint64_t seed)
{
    auto s = pink(kSr, 10.0, seed);
    s = withCoefs(s, designHighPass(kSr, 100.0, 0.707));
    s = withCoefs(s, designPeak(kSr, 350.0, 0.7, 4.0));
    s = withCoefs(s, designHighShelf(kSr, 1000.0, 0.707, -10.0));
    return s;
}

std::vector<EqSuggestion> listen(const Signal& s, gain::SourceType src, const EqSettings& current = {})
{
    EqListener l;
    l.start(6.0, kSr);
    const int n = EqListener::frameSize();
    for (size_t pos = 0; pos + static_cast<size_t>(n) <= s.size() && l.listening(); pos += static_cast<size_t>(n / 2))
        l.feedFrame(s.data() + pos);
    REQUIRE(l.ready());
    return l.suggestions(src, current);
}

const EqSuggestion* find(const std::vector<EqSuggestion>& v, const std::string& id)
{
    for (const auto& s : v)
        if (s.id == id)
            return &s;
    return nullptr;
}

} // namespace

TEST_CASE("EQ settings: defaults, validation, round-trip", "[eq]")
{
    const EqSettings d;
    CHECK_FALSE(d.enabled);
    CHECK_FALSE(d.bands[0].on);
    CHECK_FALSE(d.bands[6].on);
    CHECK(d.activeBandCount() == 0);
    EqSettings s = d;
    s.bands[3].gainDb = 99.0f;
    s.bands[1].q = 7.0f;            // shelves are limited to Q 2
    s.bands[0].slope = 40.0f;       // snaps to 48
    s.bands[2].freqHz = std::numeric_limits<float>::quiet_NaN();
    s.validate();
    CHECK(s.bands[3].gainDb == 18.0f);
    CHECK(s.bands[1].q == 2.0f);
    CHECK(s.bands[0].slope == 48.0f);
    CHECK(s.bands[2].freqHz == d.bands[2].freqHz);
    s.enabled = true;
    s.bands[4].gainDb = -3.5f;
    CHECK(EqSettings::deserialise(s.serialise()) == s);
    CHECK(EqSettings::deserialise("junk") == EqSettings {});
    for (const auto& p : eqProfiles())
    {
        auto v = p.settings;
        v.validate();
        CHECK(v == p.settings);
        CHECK(p.settings.enabled);
    }
}

TEST_CASE("Bells and shelves have the designed gain", "[eq][filters]")
{
    for (double f : { 100.0, 1000.0, 8000.0 })
        for (double g : { -12.0, -3.0, 6.0, 15.0 })
        {
            EqSettings s = enabledFlat();
            s.bands[3] = { true, static_cast<float>(f), static_cast<float>(g), 2.0f, 24.0f };
            INFO("bell " << f << " Hz " << g << " dB");
            CHECK(measureDb(s, f) == Approx(g).margin(0.1));
            CHECK(std::abs(measureDb(s, f * 16.0 > 20000.0 ? f / 16.0 : f * 16.0)) < 0.3);
        }
    EqSettings ls = enabledFlat();
    ls.bands[1] = { true, 200.0f, 6.0f, 0.707f, 24.0f };
    CHECK(measureDb(ls, 25.0) == Approx(6.0).margin(0.2));
    CHECK(measureDb(ls, 10000.0) == Approx(0.0).margin(0.1));
    EqSettings hs = enabledFlat();
    hs.bands[5] = { true, 5000.0f, -8.0f, 0.707f, 24.0f };
    CHECK(measureDb(hs, 18000.0) == Approx(-8.0).margin(0.3));
    CHECK(measureDb(hs, 100.0) == Approx(0.0).margin(0.1));
}

TEST_CASE("High- and low-pass: -3 dB at the corner and the stated slope", "[eq][filters]")
{
    for (float slope : { 12.0f, 24.0f, 48.0f })
    {
        EqSettings hp = enabledFlat();
        hp.bands[0] = { true, 200.0f, 0.0f, 0.707f, slope };
        INFO("slope " << slope);
        CHECK(measureDb(hp, 200.0) == Approx(-3.01).margin(0.1));
        // Two octaves below the corner: Butterworth, 10 log10(1 + 4^(2n)) ~ 2 x slope.
        const double order = static_cast<double>(slope) / 6.0;
        CHECK(measureDb(hp, 50.0) == Approx(-10.0 * std::log10(1.0 + std::pow(4.0, 2.0 * order))).margin(0.5));
        CHECK(measureDb(hp, 5000.0) == Approx(0.0).margin(0.05));

        EqSettings lp = enabledFlat();
        lp.bands[6] = { true, 2000.0f, 0.0f, 0.707f, slope };
        CHECK(measureDb(lp, 2000.0) == Approx(-3.01).margin(0.1));
        CHECK(measureDb(lp, 50.0) == Approx(0.0).margin(0.05));
        CHECK(measureDb(lp, 8000.0) < -1.8 * static_cast<double>(slope));
    }
}

TEST_CASE("The drawn curve (responseDb) matches what the EQ does", "[eq][filters]")
{
    Rng rng(42);
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int trial = 0; trial < 4; ++trial)
        {
            EqSettings s = enabledFlat();
            for (int b = 0; b < kNumBands; ++b)
            {
                auto& band = s.bands[static_cast<size_t>(b)];
                band.on = rng.uniform() > 0.3f;
                band.freqHz = static_cast<float>(40.0 * std::pow(300.0, static_cast<double>(rng.uniform())));
                band.gainDb = (rng.uniform() - 0.5f) * 24.0f;
                band.q = 0.4f + rng.uniform() * 3.0f;
                band.slope = rng.uniform() > 0.5f ? 24.0f : 12.0f;
            }
            s.bands[0].freqHz = 40.0f;   // keep the pass filters away from the test tones
            s.bands[6].freqHz = 16000.0f;
            s.outputDb = 1.5f;
            for (double f : { 80.0, 315.0, 1000.0, 3150.0, 9000.0 })
            {
                INFO("sr " << sr << " trial " << trial << " f " << f);
                CHECK(measureDb(s, f, sr) == Approx(responseDb(s, sr, f)).margin(0.15));
            }
        }
}

TEST_CASE("EQ off = bit-exact, zero latency; flat EQ is transparent", "[eq]")
{
    const auto in = pink(kSr, 1.0, 3);
    EqSettings off;
    off.bands[3].gainDb = 12.0f;
    ParametricEq e;
    setup(e, off);
    CHECK(e.latencySamples() == 0);
    CHECK(run(e, in) == in);

    ParametricEq flat;
    setup(flat, enabledFlat());
    const auto out = run(flat, in);
    double maxDiff = 0.0;
    for (size_t i = 0; i < in.size(); ++i)
        maxDiff = std::max(maxDiff, static_cast<double>(std::abs(out[i] - in[i])));
    CHECK(maxDiff < 1.0e-5);

    ParametricEq bypassed;
    setup(bypassed, enabledFlat());
    bypassed.setBypassed(true);
    bypassed.reset();
    CHECK(run(bypassed, in) == in);
}

TEST_CASE("Moving a band or switching it never clicks", "[eq][smoothing]")
{
    const auto in = sine(kSr, 200.0, 2.0, 0.5);
    float maxInStep = 0.0f;
    for (size_t i = 1; i < in.size(); ++i)
        maxInStep = std::max(maxInStep, std::abs(in[i] - in[i - 1]));

    for (int scenario = 0; scenario < 3; ++scenario)
    {
        EqSettings s = enabledFlat();
        s.bands[2] = { true, 200.0f, 0.0f, 1.0f, 24.0f };
        ParametricEq e;
        setup(e, s);
        Signal out = in;
        for (size_t pos = 0; pos < out.size(); pos += 128)
        {
            if (pos == 48000 / 128 * 128)
            {
                if (scenario == 0)
                    s.bands[2].gainDb = 15.0f;  // big gain jump
                else if (scenario == 1)
                    s.bands[2].freqHz = 2000.0f, s.bands[2].gainDb = -15.0f; // frequency sweep through the tone
                else
                    s.bands[0] = { true, 400.0f, 0.0f, 0.707f, 48.0f };     // steep high-pass switched on
                e.setSettings(s);
            }
            float* ch[1] = { out.data() + pos };
            e.process(ch, 1, static_cast<int>(std::min<size_t>(128, out.size() - pos)));
        }
        float maxStep = 0.0f;
        for (size_t i = 1; i < out.size(); ++i)
            maxStep = std::max(maxStep, std::abs(out[i] - out[i - 1]));
        INFO("scenario " << scenario);
        // The largest step stays within what the louder (boosted) tone itself produces.
        CHECK(maxStep <= maxInStep * static_cast<float>(scenario == 0 ? 5.7 : 1.05));
        for (float v : out)
            REQUIRE(std::isfinite(v));
    }
}

TEST_CASE("EQ output is independent of block size; stable at extremes", "[eq]")
{
    EqSettings s = enabledFlat();
    s.bands[0] = { true, 20.0f, 0.0f, 0.707f, 48.0f };
    s.bands[2] = { true, 20.0f, 18.0f, 10.0f, 24.0f };
    s.bands[3] = { true, 1000.0f, -18.0f, 0.1f, 24.0f };
    s.bands[4] = { true, 20000.0f, 18.0f, 10.0f, 24.0f };
    s.bands[6] = { true, 20000.0f, 0.0f, 10.0f, 12.0f };
    const auto in = pink(kSr, 2.0, 9, 0.05f);
    ParametricEq a, b;
    setup(a, s);
    setup(b, s);
    const auto oa = run(a, in, 64);
    CHECK(oa == run(b, in, 999));
    for (float v : oa)
        REQUIRE(std::isfinite(v));
    for (double sr : { 22050.0, 44100.0, 96000.0, 192000.0 })
    {
        ParametricEq c;
        setup(c, s, sr);
        const auto o = run(c, pink(sr, 1.0, 11, 0.05f));
        for (float v : o)
            REQUIRE(std::isfinite(v));
        CHECK(*std::max_element(o.begin(), o.end()) < 100.0f);
    }
}

TEST_CASE("Analyzer: FFT and band levels", "[eq][spectrum]")
{
    Fft fft(12);
    const auto s = sine(kSr, 1000.0, 0.2, 1.0);
    const auto p = powerSpectrum(s.data(), fft);
    const auto peak = std::max_element(p.begin(), p.end());
    const double binHz = kSr / 4096.0;
    CHECK(static_cast<double>(peak - p.begin()) * binHz == Approx(1000.0).margin(binHz));
    CHECK(10.0 * std::log10(static_cast<double>(*peak)) == Approx(0.0).margin(1.6)); // Hann scalloping < 1.5 dB
    const auto levels = thirdOctaveLevelsDb(p, kSr);
    const auto& c = thirdOctaveCentres();
    REQUIRE(c.size() == 31);
    const auto loudest = static_cast<size_t>(std::max_element(levels.begin(), levels.end()) - levels.begin());
    CHECK(c[loudest] == Approx(1000.0).epsilon(0.01));

    SpectrumTap tap;
    for (int i = 0; i < 1000; ++i)
        tap.push(static_cast<float>(i));
    std::vector<float> last(10);
    CHECK(tap.readLatest(last.data(), 10) == 1000u);
    CHECK(last.front() == 990.0f);
    CHECK(last.back() == 999.0f);
}

TEST_CASE("EQ Coach finds a resonance and suggests a narrow cut", "[eq][coach]")
{
    const auto material = withBell(pink(kSr, 10.0, 21), 2000.0, 8.0, 10.0);
    const auto sugg = listen(material, gain::SourceType::Instrument);
    const auto* r = find(sugg, "resonance");
    REQUIRE(r != nullptr);
    INFO(r->title);
    CHECK(r->settings.freqHz == Approx(2000.0f).epsilon(0.06));
    CHECK(r->settings.gainDb <= -1.5f);
    CHECK(r->settings.gainDb >= -6.0f);
    CHECK(r->band == 3);
    CHECK(r->settings.on);
    CHECK(r->why.find("drum") == std::string::npos); // source-aware wording

    // Same material labelled as a vocal: no drum wording; as a drum: drum wording.
    const auto vocalSugg = listen(material, gain::SourceType::Vocal);
    const auto* rv = find(vocalSugg, "resonance");
    if (rv != nullptr)
        CHECK(rv->why.find("drum") == std::string::npos);
    const auto drumSugg = listen(material, gain::SourceType::Drum);
    const auto* rd = find(drumSugg, "resonance");
    REQUIRE(rd != nullptr);
    CHECK(rd->why.find("drum") != std::string::npos);
}

TEST_CASE("EQ Coach: mud on a vocal, rumble -> high-pass, nothing invented on clean material", "[eq][coach]")
{
    // Instrument (flat reference): a broad low-mid bump is mud.
    const auto muddy = withBell(pink(kSr, 10.0, 31), 320.0, 0.9, 7.0);
    const auto s1 = listen(muddy, gain::SourceType::Instrument);
    const auto* mud = find(s1, "mud");
    REQUIRE(mud != nullptr);
    CHECK(mud->band == 2);
    CHECK(mud->settings.gainDb < 0.0f);
    CHECK(mud->settings.gainDb >= -5.0f);

    // A typical vocal spectrum is not flagged; the same vocal with extra low-mid build-up is.
    const auto vocal = vocalShaped(61);
    const auto v1 = listen(vocal, gain::SourceType::Vocal);
    for (const auto& x : v1)
        UNSCOPED_INFO("clean vocal: " << x.id << " - " << x.title);
    CHECK(find(v1, "mud") == nullptr);
    CHECK(find(v1, "harsh") == nullptr);
    CHECK(find(v1, "resonance") == nullptr);
    const auto v2 = listen(withBell(vocal, 300.0, 0.9, 7.0), gain::SourceType::Vocal);
    CHECK(find(v2, "mud") != nullptr);

    // Clean pink noise on a drum: no resonance or mud suggestions.
    const auto s2 = listen(pink(kSr, 10.0, 41), gain::SourceType::Drum);
    CHECK(find(s2, "resonance") == nullptr);
    CHECK(find(s2, "mud") == nullptr);

    // Rumble on a vocal: high-pass suggested, unless one is already set.
    auto rumbly = pink(kSr, 10.0, 51);
    const auto hum = sine(kSr, 35.0, 10.0, 0.3);
    for (size_t i = 0; i < rumbly.size(); ++i)
        rumbly[i] += hum[i];
    const auto s3 = listen(rumbly, gain::SourceType::Vocal);
    const auto* hp = find(s3, "hpf-rumble");
    REQUIRE(hp != nullptr);
    CHECK(hp->band == 0);
    CHECK(hp->settings.freqHz == Approx(90.0f));
    EqSettings already;
    already.bands[0] = { true, 100.0f, 0.0f, 0.707f, 24.0f };
    CHECK(find(listen(rumbly, gain::SourceType::Vocal, already), "hpf-rumble") == nullptr);

    // Never more than 5, never two for the same band.
    std::vector<bool> used(kNumBands, false);
    for (const auto& s : s3)
    {
        CHECK_FALSE(used[static_cast<size_t>(s.band)]);
        used[static_cast<size_t>(s.band)] = true;
    }
    CHECK(s3.size() <= 5);
}

TEST_CASE("EQ Coach ignores silence", "[eq][coach]")
{
    EqListener l;
    l.start(2.0, kSr);
    std::vector<float> quiet(static_cast<size_t>(EqListener::frameSize()), 1.0e-4f);
    for (int i = 0; i < 100; ++i)
        l.feedFrame(quiet.data());
    CHECK(l.listening());
    CHECK(l.progress() == 0.0f);
    CHECK(l.suggestions(gain::SourceType::Vocal, {}).empty());
}
