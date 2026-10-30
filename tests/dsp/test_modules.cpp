// Module power (on/off) and source-aware wording.

#include "../support/DrumSignals.h"

#include "mixcore/strip/ChannelPresets.h"
#include "mixcore/strip/StripCoach.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <string>

using namespace mixcore;
using namespace mixcore::test;

namespace {
std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
std::string allText(const std::vector<gain::Advice>& advice)
{
    std::string t;
    for (const auto& a : advice)
        t += a.detected + " " + a.whyItMatters + " " + a.whatToCheck + " " + a.recommendation + " " + a.automaticNote + "\n";
    return lower(t);
}

/// Telemetry that triggers every source-dependent Smart Gain rule.
gain::SmartGainTelemetry everythingTelemetry()
{
    gain::SmartGainTelemetry t;
    t.activity = gain::ActivityState::Active;
    t.inputRmsDb = -50.0f;
    t.peakRestrictionActive = true;
    t.riderRestrictionDb = 2.0f;
    t.riderEnabled = true;
    t.targetErrorValid = true;
    t.targetErrorDb = 6.0f;
    t.riderGainDb = 12.0f;
    t.calibration = gain::CalibrationState::Collecting;
    return t;
}
} // namespace

TEST_CASE("Only Smart Gain is on by default", "[modules]")
{
    const strip::ChannelSettings c;
    CHECK(c.gainOn);
    CHECK_FALSE(c.gate.enabled);
    CHECK_FALSE(c.drum.debleedEnabled);
    CHECK(c.source == gain::SourceType::Vocal);
}

TEST_CASE("A module that is off leaves the audio untouched", "[modules]")
{
    const double sr = 48000.0;
    strip::ChannelStrip st;
    REQUIRE(st.prepare(sr, 256, 1));
    strip::ChannelSettings c;
    c.gain.staticGainDb = 9.0f; // would be obvious if applied
    c.gainOn = false;
    st.setSettings(c);
    st.reset();
    REQUIRE(st.latencySamples() == 0);
    const auto in = snareGroove(sr, 2.0, 120.0, 0.3f, -50.0, 3);
    auto out = in;
    for (size_t pos = 0; pos < out.size(); pos += 256)
    {
        float* ch[1] = { out.data() + pos };
        st.process(ch, 1, static_cast<int>(std::min<size_t>(256, out.size() - pos)));
    }
    CHECK(out == in); // Smart Gain off, gate off, de-bleed off: bit-exact

    // Switching Smart Gain on applies it (after the 10 ms fade).
    c.gainOn = true;
    st.setSettings(c);
    std::vector<float> x(4800, 0.1f);
    float* ch[1] = { x.data() };
    st.process(ch, 1, 4800);
    CHECK(x.back() > 0.1f * 2.5f); // +9 dB ~ x2.8
}

TEST_CASE("Mix Coach words follow the source type", "[modules][coach]")
{
    const auto t = everythingTelemetry();
    gain::SmartGainSettings s;
    const auto vocal = allText(gain::evaluateSmartGainAdvice(t, s, gain::SourceType::Vocal));
    const auto drum = allText(gain::evaluateSmartGainAdvice(t, s, gain::SourceType::Drum));
    const auto speech = allText(gain::evaluateSmartGainAdvice(t, s, gain::SourceType::Speech));
    CHECK(vocal.find("vocal") != std::string::npos);
    CHECK(drum.find("vocal") == std::string::npos);
    CHECK(drum.find("singer") == std::string::npos);
    CHECK(drum.find(" sing ") == std::string::npos);
    CHECK(drum.find("drum") != std::string::npos);
    CHECK(speech.find("vocal") == std::string::npos);
    CHECK(speech.find("voice") != std::string::npos);
}

TEST_CASE("Strip coach: no Smart Gain advice when Smart Gain is off; drum wording for drums", "[modules][coach]")
{
    const auto t = everythingTelemetry();
    gate::GateTelemetry gt;
    gt.enabled = true;
    gt.state = gate::GateState::Closed;
    gt.closedSeconds = 5.0f;
    gt.hardGate = true;

    strip::ChannelSettings c = strip::findChannelPreset("Kick Drum")->settings;
    REQUIRE(c.source == gain::SourceType::Drum);
    const auto drumText = allText(strip::evaluateStripAdvice(t, gt, c));
    CHECK(drumText.find("vocal") == std::string::npos);
    CHECK(drumText.find("words") == std::string::npos);
    CHECK(drumText.find("drum") != std::string::npos);

    c.gainOn = false;
    for (const auto& a : strip::evaluateStripAdvice(t, gt, c))
        CHECK(a.id.rfind("gate-", 0) == 0); // only gate advice remains
}

TEST_CASE("Channel presets set the source type", "[modules][presets]")
{
    for (const auto& p : strip::channelPresets())
    {
        INFO(p.name);
        CHECK(p.settings.gainOn);
        if (p.category == "Drums")
            CHECK(p.settings.source == gain::SourceType::Drum);
        else if (p.category == "Speech")
            CHECK(p.settings.source == gain::SourceType::Speech);
        else
            CHECK(p.settings.source == gain::SourceType::Vocal);
    }
}
