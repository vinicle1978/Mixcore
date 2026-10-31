// Compressor, Comp Coach and compressor profile tests.

#include "../support/TestSignals.h"

#include "mixcore/comp/CompCoach.h"
#include "mixcore/comp/CompProfiles.h"
#include "mixcore/comp/Compressor.h"
#include "mixcore/strip/ChannelPresets.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

using namespace mixcore;
using namespace mixcore::comp;
using namespace mixcore::test;
using Catch::Approx;

namespace {

constexpr double kSr = 48000.0;

CompSettings hard(float t, float r)
{
    CompSettings s;
    s.enabled = true;
    s.thresholdDb = t;
    s.ratio = r;
    s.kneeDb = 0.0f;
    s.scHpfOn = false;
    return s;
}

void setup(Compressor& c, const CompSettings& s, int ch = 1)
{
    REQUIRE(c.prepare(kSr, 512, ch));
    c.setSettings(s);
    c.reset();
}

Signal run(Compressor& c, const Signal& in, int block = 256)
{
    Signal out = in;
    for (size_t pos = 0; pos < out.size(); pos += static_cast<size_t>(block))
    {
        float* ch[1] = { out.data() + pos };
        c.process(ch, 1, static_cast<int>(std::min(out.size() - pos, static_cast<size_t>(block))));
    }
    return out;
}

/// Constant (DC) signal with level steps: makes the gain directly readable (out / in).
Signal steps(const std::vector<std::pair<double, float>>& parts)
{
    Signal s;
    for (const auto& [seconds, value] : parts)
        s.insert(s.end(), static_cast<size_t>(seconds * kSr), value);
    return s;
}

double gainDbAt(const Signal& in, const Signal& out, double seconds)
{
    const auto i = static_cast<size_t>(seconds * kSr);
    return 20.0 * std::log10(static_cast<double>(out[i]) / static_cast<double>(in[i]));
}

/// Level material: alternating loud and quiet "phrases" of noise, with silent gaps.
Signal phrases(float loudDb, float quietDb, int count, std::uint64_t seed)
{
    Signal s;
    for (int i = 0; i < count; ++i)
    {
        const auto n = noise(kSr, 0.6, static_cast<double>(i % 2 == 0 ? loudDb : quietDb), seed + static_cast<std::uint64_t>(i));
        s.insert(s.end(), n.begin(), n.end());
        s.insert(s.end(), static_cast<size_t>(0.2 * kSr), 0.0f);
    }
    return s;
}

std::vector<CompSuggestion> listen(const Signal& material, gain::SourceType src, const CompSettings& current, double seconds = 4.0)
{
    Compressor c;
    setup(c, current);
    CompListener l;
    l.start(seconds, c.levelTap());
    Signal buf = material;
    for (size_t pos = 0; pos < buf.size() && l.listening(); pos += 480)
    {
        float* ch[1] = { buf.data() + pos };
        c.process(ch, 1, static_cast<int>(std::min<size_t>(480, buf.size() - pos)));
        l.pump(c.levelTap());
    }
    REQUIRE(l.ready());
    return l.suggestions(src, current);
}

const CompSuggestion* find(const std::vector<CompSuggestion>& v, const std::string& id)
{
    for (const auto& s : v)
        if (s.id == id)
            return &s;
    return nullptr;
}

} // namespace

TEST_CASE("Compressor settings: defaults, validation, round-trip", "[comp][settings]")
{
    CompSettings d;
    CHECK_FALSE(d.enabled);
    CHECK(d.ratio == 3.0f);

    CompSettings s;
    s.thresholdDb = -100.0f;
    s.ratio = 0.5f;
    s.attackMs = std::nanf("");
    s.releaseMs = 99999.0f;
    s.mixPercent = 140.0f;
    s.scHpfHz = 5.0f;
    s.validate();
    CHECK(s.thresholdDb == -60.0f);
    CHECK(s.ratio == 1.0f);
    CHECK(s.attackMs == d.attackMs);
    CHECK(s.releaseMs == 2000.0f);
    CHECK(s.mixPercent == 100.0f);
    CHECK(s.scHpfHz == 20.0f);

    CompSettings x;
    x.enabled = true;
    x.thresholdDb = -27.5f;
    x.ratio = 6.0f;
    x.kneeDb = 3.0f;
    x.attackMs = 2.5f;
    x.releaseMs = 333.0f;
    x.autoRelease = true;
    x.makeupDb = 4.5f;
    x.autoMakeup = true;
    x.mixPercent = 37.0f;
    x.scHpfOn = false;
    x.scHpfHz = 150.0f;
    x.detector = Detector::Peak;
    CHECK(CompSettings::deserialise(x.serialise()) == x);
    CHECK(CompSettings::deserialise("garbage;;ratio=abc;") == CompSettings {});
}

TEST_CASE("Static curve: threshold, ratio and soft knee", "[comp][curve]")
{
    CHECK(gainChangeDb(-30.0f, -20.0f, 4.0f, 0.0f) == 0.0f);
    CHECK(gainChangeDb(-8.0f, -20.0f, 4.0f, 0.0f) == Approx(-9.0f));
    CHECK(gainChangeDb(-8.0f, -20.0f, 1.0f, 6.0f) == 0.0f);
    // Soft knee: continuous at both edges, 1/4 of the way at the threshold, monotonic.
    CHECK(gainChangeDb(-23.0f, -20.0f, 4.0f, 6.0f) == Approx(0.0f).margin(1e-6));
    CHECK(gainChangeDb(-17.0f, -20.0f, 4.0f, 6.0f) == Approx(-0.75f * 3.0f).margin(1e-5));
    CHECK(gainChangeDb(-20.0f, -20.0f, 4.0f, 6.0f) == Approx(-0.75f * 3.0f / 4.0f).margin(1e-5));
    float prevOut = -100.0f;
    for (float x = -40.0f; x <= 0.0f; x += 0.1f)
    {
        const float out = x + gainChangeDb(x, -20.0f, 4.0f, 12.0f);
        CHECK(out >= prevOut - 1e-5f);
        prevOut = out;
    }
    // Threshold solver inverts the curve.
    for (float knee : { 0.0f, 6.0f, 12.0f })
    {
        const float t = thresholdForReduction(-10.0f, 5.0f, 3.0f, knee);
        CHECK(-gainChangeDb(-10.0f, t, 3.0f, knee) == Approx(5.0f).margin(0.01));
    }
}

TEST_CASE("Steady-state reduction matches the ratio (RMS detector)", "[comp][dsp]")
{
    auto s = hard(-20.0f, 4.0f);
    s.detector = Detector::Rms;
    Compressor c;
    setup(c, s);
    // 1 kHz sine at -8 dBFS RMS (MixCore scale) -> 9 dB reduction.
    const double amp = std::sqrt(2.0) * std::pow(10.0, -8.0 / 20.0);
    const auto in = sine(kSr, 1000.0, 1.0, amp);
    const auto out = run(c, in);
    CHECK(rmsDb(out, in.size() / 2) - rmsDb(in, in.size() / 2) == Approx(-9.0).margin(0.25));

    // Below threshold: untouched.
    Compressor c2;
    setup(c2, s);
    const auto quiet = sine(kSr, 1000.0, 0.5, 0.01);
    const auto q = run(c2, quiet);
    CHECK(rmsDb(q, q.size() / 2) == Approx(rmsDb(quiet, quiet.size() / 2)).margin(0.01));
}

TEST_CASE("Attack and release follow their time constants", "[comp][dsp]")
{
    // Peak detector on DC: target 18 dB reduction (-6 dBFS, threshold -30, 4:1).
    auto s = hard(-30.0f, 4.0f);
    s.detector = Detector::Peak;
    s.attackMs = 20.0f;
    s.releaseMs = 100.0f;
    Compressor c;
    setup(c, s);
    const auto in = steps({ { 0.2, 0.01f }, { 0.5, 0.5f }, { 0.6, 0.01f } });
    const auto out = run(c, in);
    CHECK(gainDbAt(in, out, 0.1) == Approx(0.0).margin(1e-4));
    CHECK(gainDbAt(in, out, 0.2 + 0.020) == Approx(-18.0 * (1.0 - std::exp(-1.0))).margin(0.3)); // one time constant
    CHECK(gainDbAt(in, out, 0.69) == Approx(-18.0).margin(0.05));
    CHECK(gainDbAt(in, out, 0.7 + 0.100) == Approx(-18.0 * std::exp(-1.0)).margin(0.3));
    CHECK(gainDbAt(in, out, 1.29) == Approx(0.0).margin(0.05));
}

TEST_CASE("Auto release: fast after short peaks, slow after sustained compression", "[comp][dsp]")
{
    auto s = hard(-30.0f, 4.0f);
    s.detector = Detector::Peak;
    s.attackMs = 1.0f;
    s.releaseMs = 50.0f;
    s.autoRelease = true;
    const auto measure = [&](double burst) {
        Compressor c;
        setup(c, s);
        const auto in = steps({ { 0.1, 0.01f }, { burst, 0.5f }, { 1.0, 0.01f } });
        const auto out = run(c, in);
        return -gainDbAt(in, out, 0.1 + burst + 0.15); // reduction 150 ms after the loud part ends
    };
    const double afterShort = measure(0.02);
    const double afterLong = measure(3.0);
    INFO("short " << afterShort << " dB, long " << afterLong << " dB");
    CHECK(afterShort < 1.0);
    CHECK(afterLong > 5.0 * afterShort);
    CHECK(afterLong > 4.0);

    // Without auto release both release at the same speed.
    s.autoRelease = false;
    CHECK(measure(3.0) == Approx(measure(0.5)).margin(0.05));
}

TEST_CASE("Makeup: manual and auto", "[comp][dsp]")
{
    auto s = hard(-20.0f, 4.0f);
    s.makeupDb = 6.0f;
    Compressor c;
    setup(c, s);
    const auto in = steps({ { 0.3, 0.01f } }); // below threshold
    CHECK(gainDbAt(in, run(c, in), 0.25) == Approx(6.0).margin(0.01));

    // Auto makeup brings the level of compressed material back to about where it was.
    auto a = hard(-30.0f, 4.0f);
    a.autoMakeup = true;
    a.detector = Detector::Rms;
    Compressor c2;
    setup(c2, a);
    const auto material = phrases(-14.0f, -20.0f, 16, 3);
    const auto out = run(c2, material);
    const auto from = material.size() / 2;
    const double change = rmsDb(out, from) - rmsDb(material, from);
    INFO("level change with auto makeup " << change << " dB");
    CHECK(std::abs(change) < 2.0); // "about the same": average dB reduction vs. RMS loss differ slightly
    CompTelemetry t;
    REQUIRE(c2.readTelemetry(t));
    CHECK(t.makeupDb > 8.0f);
}

TEST_CASE("Parallel mix and off are exact; switching never clicks", "[comp][dsp]")
{
    auto s = hard(-30.0f, 4.0f);
    s.detector = Detector::Peak;
    s.attackMs = 0.1f;
    s.mixPercent = 50.0f;
    Compressor c;
    setup(c, s);
    const auto in = steps({ { 0.5, 0.5f } });
    const auto out = run(c, in);
    const double wet = std::pow(10.0, -18.0 / 20.0);
    CHECK(static_cast<double>(out.back()) / 0.5 == Approx(0.5 * (1.0 + wet)).margin(1e-3));

    s.mixPercent = 0.0f;
    Compressor dry;
    setup(dry, s);
    CHECK(run(dry, in) == in);

    CompSettings off = s;
    off.enabled = false;
    Compressor o;
    setup(o, off);
    const auto n = noise(kSr, 0.5, -10.0, 4);
    CHECK(run(o, n) == n);

    // Switch on and off mid-signal: the gain moves smoothly (10 ms crossfade).
    auto on = hard(-30.0f, 8.0f);
    on.attackMs = 0.1f;
    on.detector = Detector::Peak;
    Compressor sw;
    setup(sw, off);
    Signal dc(static_cast<size_t>(kSr), 0.5f);
    float prev = 0.5f, maxStep = 0.0f;
    for (size_t pos = 0; pos < dc.size(); pos += 256)
    {
        sw.setSettings(((pos / 4800) % 2) ? on : off);
        float* ch[1] = { dc.data() + pos };
        sw.process(ch, 1, static_cast<int>(std::min<size_t>(256, dc.size() - pos)));
        for (size_t i = pos; i < std::min(pos + 256, dc.size()); ++i)
        {
            maxStep = std::max(maxStep, std::abs(dc[i] - prev));
            prev = dc[i];
        }
    }
    INFO("largest sample-to-sample step " << maxStep);
    CHECK(maxStep < 0.01f); // a hard switch would jump by ~0.4
}

TEST_CASE("Sidechain high-pass stops low end driving the compressor", "[comp][dsp]")
{
    const auto bass = sine(kSr, 40.0, 1.0, 0.5);
    const auto reduction = [&](bool hpf) {
        auto s = hard(-24.0f, 4.0f);
        s.scHpfOn = hpf;
        s.scHpfHz = 150.0f;
        Compressor c;
        setup(c, s);
        const auto out = run(c, bass);
        return rmsDb(bass, bass.size() / 2) - rmsDb(out, out.size() / 2);
    };
    const double without = reduction(false), with = reduction(true);
    INFO("without HPF " << without << " dB, with " << with << " dB");
    CHECK(without > 10.0);
    CHECK(with < without - 8.0);
}

TEST_CASE("Stereo link, block-size independence, extremes", "[comp][dsp]")
{
    auto s = hard(-24.0f, 4.0f);
    Compressor c;
    setup(c, s, 2);
    auto l = noise(kSr, 1.0, -8.0, 7), r = noise(kSr, 1.0, -30.0, 8);
    const auto l0 = l, r0 = r;
    for (size_t pos = 0; pos < l.size(); pos += 256)
    {
        float* ch[2] = { l.data() + pos, r.data() + pos };
        c.process(ch, 2, static_cast<int>(std::min<size_t>(256, l.size() - pos)));
    }
    for (size_t i = 24000; i < l.size(); i += 997)
        if (std::abs(l0[i]) > 1e-3f && std::abs(r0[i]) > 1e-3f)
            CHECK(l[i] / l0[i] == Approx(r[i] / r0[i]).epsilon(1e-4)); // same gain on both channels

    const auto material = phrases(-10.0f, -25.0f, 6, 9);
    auto p = hard(-26.0f, 5.0f);
    p.autoRelease = true;
    p.autoMakeup = true;
    p.scHpfOn = true;
    Compressor a, b;
    setup(a, p);
    setup(b, p);
    CHECK(run(a, material, 64) == run(b, material, 997));

    for (const auto& [t, r2, att, rel] : { std::tuple { -60.0f, 20.0f, 0.1f, 10.0f }, std::tuple { 0.0f, 1.0f, 100.0f, 2000.0f } })
    {
        auto e = hard(t, r2);
        e.attackMs = att;
        e.releaseMs = rel;
        e.kneeDb = 24.0f;
        e.makeupDb = 24.0f;
        Compressor x;
        setup(x, e);
        const auto o = run(x, noise(kSr, 0.5, -1.0, 11));
        CHECK(std::all_of(o.begin(), o.end(), [](float v) { return std::isfinite(v); }));
    }
}

TEST_CASE("Telemetry and the level tap", "[comp][telemetry]")
{
    auto s = hard(-30.0f, 4.0f);
    s.detector = Detector::Peak;
    s.attackMs = 0.1f;
    Compressor c;
    setup(c, s);
    run(c, steps({ { 0.5, 0.5f } }));
    CompTelemetry t;
    REQUIRE(c.readTelemetry(t));
    CHECK(t.enabled);
    CHECK(t.gainReductionDb == Approx(18.0f).margin(0.1));
    CHECK(t.detectorDb == Approx(gainToDb(0.5f)).margin(0.05));
    CHECK(c.levelTap().written() == 50u); // one entry per 10 ms
    CHECK(c.levelTap().at(49).rmsDb == Approx(powerToDb(0.25)).margin(0.01));

    // Off: the tap keeps recording (so the Coach can listen before switching on), no reduction reported.
    s.enabled = false;
    Compressor o;
    setup(o, s);
    run(o, steps({ { 0.2, 0.5f } }));
    REQUIRE(o.readTelemetry(t));
    CHECK_FALSE(t.enabled);
    CHECK(t.gainReductionDb == 0.0f);
    CHECK(o.levelTap().written() == 20u);
}

TEST_CASE("Comp Coach suggests a threshold for a typical amount of reduction", "[comp][coach]")
{
    CompSettings current; // off, defaults
    const auto material = phrases(-12.0f, -24.0f, 20, 21);
    const auto sugg = listen(material, gain::SourceType::Vocal, current);
    const auto* t = find(sugg, "threshold");
    REQUIRE(t != nullptr);
    INFO(t->title << " / " << t->why);
    CHECK(t->settings.enabled);
    CHECK(t->settings.ratio == 3.0f);
    // Loud phrases (90th percentile of 10 ms RMS) get about 5 dB.
    CHECK(-gainChangeDb(-12.0f, t->settings.thresholdDb, t->settings.ratio, t->settings.kneeDb) == Approx(5.0f).margin(1.0));
    CHECK(t->settings.thresholdDb < -12.0f);
    CHECK(t->settings.thresholdDb > -24.0f);
    CHECK(t->settings.makeupDb > 0.0f);
    CHECK(t->why.find("phrases") != std::string::npos);
    CHECK(t->why.find("hits") == std::string::npos);

    // Even material -> light 2:1; drum wording.
    const auto even = phrases(-14.0f, -17.0f, 20, 31);
    const auto e = listen(even, gain::SourceType::Instrument, current);
    REQUIRE(find(e, "threshold") != nullptr);
    CHECK(find(e, "threshold")->settings.ratio == 2.0f);
    CHECK(find(e, "threshold")->why.find("already quite even") != std::string::npos);
}

TEST_CASE("Comp Coach: attack advice for drums and peaky sources", "[comp][coach]")
{
    // Drum hits: sharp attack, fast decay.
    Signal hits;
    Rng rng(5);
    for (int h = 0; h < 40; ++h)
        for (int i = 0; i < 12000; ++i)
        {
            const double t = i / kSr;
            hits.push_back(static_cast<float>(0.8 * std::exp(-t / 0.02) * static_cast<double>(rng.bipolar())));
        }
    CompSettings fast;
    fast.attackMs = 1.0f;
    fast.detector = Detector::Peak;
    const auto d = listen(hits, gain::SourceType::Drum, fast, 3.0);
    const auto* a = find(d, "attack");
    {
        CompListener l;
        LevelTap tap;
        l.start(3.0, tap);
        for (size_t pos = 0; pos + 480 <= hits.size() && l.listening(); pos += 480)
        {
            double sq = 0.0;
            float pk = 0.0f;
            for (size_t i = pos; i < pos + 480; ++i)
                sq += static_cast<double>(hits[i] * hits[i]), pk = std::max(pk, std::abs(hits[i]));
            l.feed({ powerToDb(sq / 480.0), gainToDb(pk) });
        }
        INFO("drum crest " << l.stats(Detector::Peak).crestDb);
        REQUIRE(a != nullptr);
    }
    CHECK(a->settings.attackMs == 15.0f);
    CHECK(a->why.find("hits") != std::string::npos);

    // Same with a slow attack already: nothing to say about attack.
    CompSettings slow = fast;
    slow.attackMs = 20.0f;
    CHECK(find(listen(hits, gain::SourceType::Drum, slow, 3.0), "attack") == nullptr);
}

TEST_CASE("Comp Coach ignores pauses and silence", "[comp][coach]")
{
    CompListener l;
    LevelTap tap;
    l.start(2.0, tap);
    for (int i = 0; i < 1000; ++i)
        l.feed({ -90.0f, -80.0f }); // silence never counts
    CHECK(l.listening());
    CHECK(l.progress() == 0.0f);
    for (int i = 0; i < 200; ++i)
        l.feed({ -20.0f, -12.0f });
    CHECK(l.ready());
    const auto st = l.stats(Detector::Rms);
    CHECK(st.quietDb == -20.0f);
    CHECK(st.crestDb == Approx(8.0f));
}

TEST_CASE("Compressor profiles are valid and switch the compressor on", "[comp][profiles]")
{
    std::set<std::string> names;
    for (const auto& p : compProfiles())
    {
        auto v = p.settings;
        v.validate();
        CHECK(v == p.settings);
        CHECK(p.settings.enabled);
        CHECK(names.insert(p.name).second);
        CHECK(findCompProfile(p.name) == &p);
    }
    CHECK(names.size() == 8);
    CHECK(findCompProfile("Kick")->settings.detector == Detector::Peak);
    CHECK(findCompProfile("Nope") == nullptr);
}

TEST_CASE("Strip: compressor runs last, adds no latency; presets prepare it switched off", "[comp][strip]")
{
    using namespace mixcore::strip;
    ChannelStrip strip;
    REQUIRE(strip.prepare(kSr, 256, 1));
    ChannelSettings s;
    s.gainOn = false;
    s.eq.enabled = true;
    s.eq.bands[3].freqHz = 1000.0f;
    s.eq.bands[3].gainDb = 12.0f; // EQ boost before the compressor
    s.comp = hard(-20.0f, 4.0f);
    strip.setSettings(s);
    strip.reset();
    const int latencyBefore = strip.latencySamples();
    const double amp = std::sqrt(2.0) * std::pow(10.0, -20.0 / 20.0); // -20 dBFS RMS in, +12 dB by the EQ
    auto sig = sine(kSr, 1000.0, 1.0, amp);
    const auto in = sig;
    for (size_t pos = 0; pos < sig.size(); pos += 256)
    {
        float* ch[1] = { sig.data() + pos };
        strip.process(ch, 1, static_cast<int>(std::min<size_t>(256, sig.size() - pos)));
    }
    // -8 dBFS reaches the compressor (EQ first): 9 dB reduction -> +3 dB overall.
    CHECK(rmsDb(sig, sig.size() / 2) - rmsDb(in, in.size() / 2) == Approx(3.0).margin(0.3));
    CHECK(strip.latencySamples() == latencyBefore);

    for (const auto& p : channelPresets())
    {
        INFO(p.name);
        CHECK_FALSE(p.settings.comp.enabled);
        CHECK_FALSE(p.compProfile.empty());
        const auto* prof = comp::findCompProfile(p.compProfile);
        REQUIRE(prof != nullptr);
        auto expected = prof->settings;
        expected.enabled = false;
        CHECK(p.settings.comp == expected);
    }
    const auto* kick = findChannelPreset("Kick Drum");
    REQUIRE(kick != nullptr);
    CHECK(kick->compProfile == "Kick");
    // Changing the compressor makes the channel differ from its preset.
    auto changed = kick->settings;
    CHECK(matchesChannelPreset(changed, kick->settings));
    changed.comp.ratio = 8.0f;
    CHECK_FALSE(matchesChannelPreset(changed, kick->settings));
}
