// Saturation, oversampling, Sat Coach and saturation profile tests.

#include "../support/TestSignals.h"

#include "mixcore/sat/SatCoach.h"
#include "mixcore/sat/SatCurves.h"
#include "mixcore/sat/SatProfiles.h"
#include "mixcore/sat/Saturator.h"
#include "mixcore/strip/ChannelPresets.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

using namespace mixcore;
using namespace mixcore::sat;
using namespace mixcore::test;
using Catch::Approx;

namespace {

constexpr double kSr = 48000.0;

SatSettings on(SatType t, float drive, bool autoGain = false, SatQuality q = SatQuality::Live)
{
    SatSettings s;
    s.enabled = true;
    s.type = t;
    s.driveDb = drive;
    s.autoGain = autoGain;
    s.quality = q;
    return s;
}

void setup(Saturator& sat, const SatSettings& s, int ch = 1)
{
    REQUIRE(sat.prepare(kSr, 512, ch));
    sat.setSettings(s);
    sat.reset();
}

Signal run(Saturator& sat, const Signal& in, int block = 256)
{
    Signal out = in;
    for (size_t pos = 0; pos < out.size(); pos += static_cast<size_t>(block))
    {
        float* ch[1] = { out.data() + pos };
        sat.process(ch, 1, static_cast<int>(std::min(out.size() - pos, static_cast<size_t>(block))));
    }
    return out;
}

/// Goertzel magnitude (linear amplitude) of `freq` over s[begin, begin + n).
double amplitudeAt(const Signal& s, size_t begin, size_t n, double freq)
{
    const double w = 2.0 * kPi * freq / kSr, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double v = static_cast<double>(s[begin + i]) + c * s1 - s2;
        s2 = s1;
        s1 = v;
    }
    const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(n);
}

/// Power of the aliased (folded, non-harmonic) components of a 7 kHz tone, relative to the fundamental (dB).
double aliasDb(const Signal& out)
{
    const size_t begin = out.size() / 2, n = 24000; // 0.5 s, 2 Hz bins: 7 kHz and all folds are on bins
    const double fund = amplitudeAt(out, begin, n, 7000.0);
    double alias = 0.0;
    for (int k = 4; k <= 40; ++k) // k = 47 would fold back onto 7 kHz
    {
        double f = std::fmod(7000.0 * k, kSr);
        if (f > kSr / 2)
            f = kSr - f;
        if (f < 20.0)
            continue;
        const double a = amplitudeAt(out, begin, n, f);
        alias += a * a;
    }
    return 10.0 * std::log10(alias / (fund * fund) + 1e-30);
}

} // namespace

TEST_CASE("Saturation settings: defaults, validation, round-trip", "[sat][settings]")
{
    SatSettings d;
    CHECK_FALSE(d.enabled);
    CHECK(d.quality == SatQuality::Live);
    SatSettings s;
    s.driveDb = 99.0f;
    s.mixPercent = -5.0f;
    s.outputDb = std::nanf("");
    s.validate();
    CHECK(s.driveDb == 36.0f);
    CHECK(s.mixPercent == 0.0f);
    CHECK(s.outputDb == 0.0f);

    SatSettings x = on(SatType::Console, 13.5f, false, SatQuality::Studio);
    x.mixPercent = 42.0f;
    x.outputDb = -3.5f;
    CHECK(SatSettings::deserialise(x.serialise()) == x);
    CHECK(SatSettings::deserialise("quality=x;") == SatSettings {});
    CHECK(SatSettings::deserialise("type=9").type == SatType::Clip); // clamped
}

TEST_CASE("Curves: unity slope at zero, bounded, monotonic; harmonic character", "[sat][curves]")
{
    for (int t = 0; t < kNumSatTypes; ++t)
    {
        const auto type = static_cast<SatType>(t);
        INFO(satTypeName(type));
        CHECK(shape(type, 1e-4f) / 1e-4f == Approx(1.0f).epsilon(1e-3));
        float prev = -10.0f;
        for (float x = -20.0f; x <= 20.0f; x += 0.01f)
        {
            const float y = shape(type, x);
            CHECK(y >= prev - 1e-6f);
            CHECK(std::abs(y) <= 1.6f);
            prev = y;
        }
    }
    // Symmetric curves: odd harmonics only. Tube: strong 2nd.
    const auto tape = harmonicsDb(SatType::Tape, -6.0f, 12.0f);
    const auto tube = harmonicsDb(SatType::Tube, -6.0f, 12.0f);
    CHECK(tape[0] < -100.0f); // 2nd
    CHECK(tape[1] > -40.0f);  // 3rd
    CHECK(tube[0] > -30.0f);
    // Clip is perfectly clean below full scale.
    CHECK(thdDb(SatType::Clip, -12.0f, 6.0f) < -100.0f);
    CHECK(thdDb(SatType::Clip, -12.0f, 18.0f) > -20.0f);
    // More drive -> more harmonics; solver inverts.
    CHECK(thdDb(SatType::Tape, -12.0f, 18.0f) > thdDb(SatType::Tape, -12.0f, 6.0f) + 10.0f);
    for (auto type : { SatType::Tape, SatType::Tube, SatType::Console })
    {
        const float d = driveForThd(type, -20.0f, -30.0f);
        CHECK(thdDb(type, -20.0f, d) == Approx(-30.0f).margin(0.1));
    }
}

TEST_CASE("Off and Mix 0 % are bit-exact; quiet signals pass at their level", "[sat][dsp]")
{
    const auto n = noise(kSr, 0.5, -12.0, 3);
    SatSettings off = on(SatType::Tube, 20.0f);
    off.enabled = false;
    Saturator a;
    setup(a, off);
    CHECK(run(a, n) == n);

    SatSettings dry = on(SatType::Tube, 20.0f);
    dry.mixPercent = 0.0f;
    Saturator b;
    setup(b, dry);
    CHECK(run(b, n) == n);

    // Quiet sine (-60 dBFS peak), drive 6 dB, no auto gain: 6 dB louder, clean.
    Saturator c;
    setup(c, on(SatType::Tape, 6.0f));
    const auto quiet = sine(kSr, 1000.0, 0.5, 0.001);
    const auto out = run(c, quiet);
    CHECK(rmsDb(out, out.size() / 2) - rmsDb(quiet, quiet.size() / 2) == Approx(6.0).margin(0.05));
}

TEST_CASE("Measured harmonics match the curve analysis", "[sat][dsp]")
{
    for (auto type : { SatType::Tape, SatType::Tube, SatType::Console })
    {
        INFO(satTypeName(type));
        Saturator s;
        setup(s, on(type, 12.0f));
        const double amp = std::pow(10.0, -12.0 / 20.0);
        const auto out = run(s, sine(kSr, 1000.0, 1.0, amp));
        const auto predicted = harmonicsDb(type, -12.0f, 12.0f);
        const double f1 = amplitudeAt(out, 24000, 24000, 1000.0);
        for (int h = 2; h <= 3; ++h)
        {
            const double measured = 20.0 * std::log10(amplitudeAt(out, 24000, 24000, 1000.0 * h) / f1 + 1e-12);
            if (predicted[static_cast<size_t>(h - 2)] > -80.0f)
                CHECK(measured == Approx(predicted[static_cast<size_t>(h - 2)]).margin(1.0));
            else
                CHECK(measured < -60.0);
        }
    }
}

TEST_CASE("Studio: 28 samples latency, transparent passband, far less aliasing", "[sat][dsp][oversampling]")
{
    Saturator st;
    setup(st, on(SatType::Tape, 0.0f, false, SatQuality::Studio));
    CHECK(st.latencySamples() == 28);
    Saturator live;
    setup(live, on(SatType::Tape, 0.0f));
    CHECK(live.latencySamples() == 0);

    // Quiet 1 kHz sine: Studio output = input delayed by exactly 28 samples.
    const auto in = sine(kSr, 1000.0, 0.5, 0.001);
    const auto out = run(st, in);
    double err = 0.0;
    for (size_t i = 12000; i < in.size(); ++i)
        err = std::max(err, std::abs(static_cast<double>(out[i]) - static_cast<double>(in[i - 28])));
    CHECK(err < 0.001 * 0.01); // < 1 % of the amplitude
    // Passband: 15 kHz within 0.5 dB.
    Saturator st2;
    setup(st2, on(SatType::Tape, 0.0f, false, SatQuality::Studio));
    const auto hi = sine(kSr, 15000.0, 0.5, 0.001);
    const auto hiOut = run(st2, hi);
    CHECK(rmsDb(hiOut, 12000) - rmsDb(hi, 12000) == Approx(0.0).margin(0.5));

    // Aliasing: 7 kHz hard-clipped by 24 dB.
    const auto tone = sine(kSr, 7000.0, 1.0, 0.5);
    Saturator l, s;
    setup(l, on(SatType::Clip, 24.0f));
    setup(s, on(SatType::Clip, 24.0f, false, SatQuality::Studio));
    const double liveAlias = aliasDb(run(l, tone)), studioAlias = aliasDb(run(s, tone));
    INFO("aliasing live " << liveAlias << " dB, studio " << studioAlias << " dB");
    CHECK(studioAlias < liveAlias - 12.0);
}

TEST_CASE("Auto gain keeps the loudness; mix and output", "[sat][dsp]")
{
    // Phrases of noise at -14 dBFS RMS, heavy Tape drive.
    Signal material;
    for (int i = 0; i < 8; ++i)
    {
        const auto n = noise(kSr, 0.5, i % 2 ? -14.0 : -20.0, 10 + static_cast<std::uint64_t>(i));
        material.insert(material.end(), n.begin(), n.end());
    }
    Saturator s;
    setup(s, on(SatType::Tape, 24.0f, true));
    const auto out = run(s, material);
    const double change = rmsDb(out, material.size() / 2) - rmsDb(material, material.size() / 2);
    INFO("level change with auto gain " << change << " dB");
    CHECK(std::abs(change) < 1.5);

    // Without auto gain the same drive is clearly louder.
    Saturator raw;
    setup(raw, on(SatType::Tape, 24.0f, false));
    const auto loud = run(raw, material);
    CHECK(rmsDb(loud, material.size() / 2) - rmsDb(material, material.size() / 2) > 6.0);

    // Output gain on a quiet, clean signal.
    auto o = on(SatType::Tape, 0.0f);
    o.outputDb = -6.0f;
    Saturator g;
    setup(g, o);
    const auto q = sine(kSr, 500.0, 0.5, 0.001);
    CHECK(rmsDb(run(g, q), 12000) - rmsDb(q, 12000) == Approx(-6.0).margin(0.05));
}

TEST_CASE("Switching on/off and changing the mix never jumps", "[sat][dsp]")
{
    const auto offS = [] {
        auto s = on(SatType::Tube, 18.0f, true);
        s.enabled = false;
        return s;
    }();
    Saturator s;
    setup(s, offS);
    auto sig = sine(kSr, 200.0, 2.0, 0.3);
    float prev = 0.0f, maxStep = 0.0f;
    // Reference: the same settings left on (a saturated wave has steeper edges than the dry sine).
    float maxNaturalStep = 0.0f;
    {
        Saturator ref;
        setup(ref, on(SatType::Tube, 18.0f, true));
        const auto steady = run(ref, sig);
        for (size_t i = 1; i < steady.size(); ++i)
            maxNaturalStep = std::max(maxNaturalStep, std::abs(steady[i] - steady[i - 1]));
    }
    for (size_t pos = 0; pos < sig.size(); pos += 256)
    {
        const size_t k = pos / 4800;
        auto st = on(SatType::Tube, 18.0f, true, SatQuality::Live);
        st.enabled = k % 2 == 1;
        st.mixPercent = (k / 2) % 2 ? 40.0f : 100.0f; // mix changes glide too
        s.setSettings(st);
        float* ch[1] = { sig.data() + pos };
        s.process(ch, 1, static_cast<int>(std::min<size_t>(256, sig.size() - pos)));
        for (size_t i = pos; i < std::min(pos + 256, sig.size()); ++i)
        {
            maxStep = std::max(maxStep, std::abs(sig[i] - prev));
            prev = sig[i];
        }
    }
    INFO("largest step " << maxStep << ", steady-state largest " << maxNaturalStep);
    CHECK(maxStep < 1.25f * maxNaturalStep); // no clicks, no loudness bump when switching on
    CHECK(std::all_of(sig.begin(), sig.end(), [](float v) { return std::isfinite(v); }));

    // Live <-> Studio while on: fades out, switches, fades back in (output stays finite and bounded).
    Saturator q;
    setup(q, on(SatType::Tape, 12.0f, true));
    auto sig2 = sine(kSr, 200.0, 1.0, 0.3);
    for (size_t pos = 0; pos < sig2.size(); pos += 256)
    {
        q.setSettings(on(SatType::Tape, 12.0f, true, (pos / 9600) % 2 ? SatQuality::Studio : SatQuality::Live));
        float* ch[1] = { sig2.data() + pos };
        q.process(ch, 1, static_cast<int>(std::min<size_t>(256, sig2.size() - pos)));
    }
    CHECK(std::all_of(sig2.begin(), sig2.end(), [](float v) { return std::isfinite(v) && std::abs(v) < 1.0f; }));
}

TEST_CASE("Saturation: block size, stereo, extremes, telemetry", "[sat][dsp]")
{
    const auto material = noise(kSr, 0.6, -10.0, 4);
    for (auto q : { SatQuality::Live, SatQuality::Studio })
    {
        Saturator a, b;
        setup(a, on(SatType::Tube, 15.0f, true, q));
        setup(b, on(SatType::Tube, 15.0f, true, q));
        CHECK(run(a, material, 64) == run(b, material, 997));
    }
    for (int t = 0; t < kNumSatTypes; ++t)
    {
        auto e = on(static_cast<SatType>(t), 36.0f, true, SatQuality::Studio);
        e.outputDb = 12.0f;
        Saturator x;
        setup(x, e);
        const auto o = run(x, noise(kSr, 0.3, 0.0, 5));
        CHECK(std::all_of(o.begin(), o.end(), [](float v) { return std::isfinite(v); }));
    }
    Saturator st;
    setup(st, on(SatType::Tape, 6.0f), 2);
    auto l = noise(kSr, 0.2, -12.0, 6), r = l;
    for (size_t pos = 0; pos < l.size(); pos += 256)
    {
        float* ch[2] = { l.data() + pos, r.data() + pos };
        st.process(ch, 2, static_cast<int>(std::min<size_t>(256, l.size() - pos)));
    }
    CHECK(l == r);

    Saturator lo, hi;
    setup(lo, on(SatType::Tape, 3.0f));
    setup(hi, on(SatType::Tape, 24.0f));
    run(lo, material);
    run(hi, material);
    SatTelemetry tl, th;
    REQUIRE(lo.readTelemetry(tl));
    REQUIRE(hi.readTelemetry(th));
    CHECK(tl.enabled);
    CHECK(th.saturationDb > tl.saturationDb + 15.0f);
    CHECK(lo.levelTap().written() == 60u);
}

TEST_CASE("Sat Coach: drive for a typical amount of harmonics, source-aware", "[sat][coach]")
{
    comp::LevelStats st;
    st.loudDb = -8.0f;
    st.typicalDb = -14.0f;
    st.quietDb = -22.0f;
    st.entries = 500;
    SatSettings current; // off, Tape, Live
    const auto v = satSuggestions(st, gain::SourceType::Vocal, current);
    REQUIRE(!v.empty());
    const auto& d = v.front();
    CHECK(d.id == "drive");
    CHECK(d.settings.enabled);
    CHECK(d.settings.autoGain);
    CHECK(thdDb(SatType::Tape, -8.0f, d.settings.driveDb) == Approx(targetThdDb(gain::SourceType::Vocal)).margin(1.0));
    CHECK(d.why.find("vocal") != std::string::npos);
    CHECK(d.why.find("drum") == std::string::npos);

    // Drums get more; a hard clipper on a voice gets a type suggestion.
    const auto dr = satSuggestions(st, gain::SourceType::Drum, current);
    CHECK(dr.front().settings.driveDb > d.settings.driveDb);
    SatSettings clip = current;
    clip.type = SatType::Clip;
    const auto vc = satSuggestions(st, gain::SourceType::Speech, clip);
    REQUIRE(vc.size() >= 2);
    CHECK(vc[0].id == "type");
    CHECK(vc[0].settings.type == SatType::Tape);
    CHECK(vc[1].settings.type == SatType::Clip); // drive suggestion changes only the drive (base = current)

    // Already heavy in Live mode -> Studio suggestion; too much -> wording says so.
    SatSettings heavy = on(SatType::Tube, 30.0f, true);
    const auto h = satSuggestions(st, gain::SourceType::Drum, heavy);
    CHECK(std::any_of(h.begin(), h.end(), [](const SatSuggestion& s) { return s.id == "quality" && s.settings.quality == SatQuality::Studio; }));
    CHECK(h.front().why.find("distorted") != std::string::npos);

    CHECK(satSuggestions(comp::LevelStats {}, gain::SourceType::Vocal, current).empty());
}

TEST_CASE("Saturation profiles are valid and switch it on", "[sat][profiles]")
{
    std::set<std::string> names;
    for (const auto& p : satProfiles())
    {
        auto v = p.settings;
        v.validate();
        CHECK(v == p.settings);
        CHECK(p.settings.enabled);
        CHECK(names.insert(p.name).second);
        CHECK(findSatProfile(p.name) == &p);
    }
    CHECK(names.size() == 8);
    CHECK(findSatProfile("Nope") == nullptr);
}

TEST_CASE("Strip: saturation runs last; Studio adds 28 samples only while on", "[sat][strip]")
{
    using namespace mixcore::strip;
    ChannelStrip strip;
    REQUIRE(strip.prepare(kSr, 256, 1));
    ChannelSettings s;
    s.gainOn = false;
    strip.setSettings(s);
    strip.reset();
    const int base = strip.latencySamples();
    s.sat = on(SatType::Tape, 6.0f, false, SatQuality::Studio);
    strip.setSettings(s);
    CHECK(strip.latencySamples() == base + 28);
    s.sat.enabled = false;
    strip.setSettings(s);
    CHECK(strip.latencySamples() == base);
    s.sat = on(SatType::Tape, 6.0f);
    strip.setSettings(s);
    CHECK(strip.latencySamples() == base);

    for (const auto& p : channelPresets())
    {
        INFO(p.name);
        CHECK_FALSE(p.settings.sat.enabled);
        const auto* prof = findSatProfile(p.satProfile);
        REQUIRE(prof != nullptr);
        auto expected = prof->settings;
        expected.enabled = false;
        CHECK(p.settings.sat == expected);
    }
    auto changed = channelPresets().front().settings;
    changed.sat.driveDb += 3.0f;
    CHECK_FALSE(matchesChannelPreset(changed, channelPresets().front().settings));
}
