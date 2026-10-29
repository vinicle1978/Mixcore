// Factory preset tests: the list is well-formed, presets never touch
// session values, and each family behaves as described on real-ish signals.

#include "../support/DrumSignals.h"
#include "../support/StageHarness.h"

#include "mixcore/gain/SmartGainPresets.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

using namespace mixcore;
using namespace mixcore::gain;
using namespace mixcore::test;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

/// Median of the 400 ms RMS windows that contain at least one hit (i.e. are
/// well above the bleed floor): the level the calibrator is designed to
/// measure on drums. Windows that only contain bleed are excluded, exactly as
/// the calibrator excludes inactive stretches.
double referenceActiveRmsDb(const Signal& s, double sr, double bleedDb)
{
    const auto w = static_cast<size_t>(0.4 * sr);
    std::vector<double> levels;
    for (size_t end = w; end <= s.size(); end += static_cast<size_t>(0.01 * sr))
        if (const double l = rmsDb(s, end - w, end); l > bleedDb + 12.0)
            levels.push_back(l);
    std::sort(levels.begin(), levels.end());
    return levels[levels.size() / 2];
}
} // namespace

TEST_CASE("Factory preset list is complete and well-formed", "[presets]")
{
    const auto& presets = factoryPresets();
    REQUIRE(presets.size() >= 10);
    CHECK(presets.front().name == "Lead Vocal - Natural");

    std::set<std::string> names, categories;
    for (const auto& p : presets)
    {
        INFO(p.name);
        CHECK(names.insert(p.name).second); // unique
        categories.insert(p.category);
        CHECK_FALSE(p.description.empty());
        // Already within every documented range.
        auto copy = p.settings;
        copy.validate();
        CHECK(copy == p.settings);
        // Presets carry no session values.
        CHECK(p.settings.staticGainDb == 0.0f);
        CHECK_FALSE(p.settings.bypass);
        CHECK(findFactoryPreset(p.name) == &p);
    }
    for (const char* c : { "Lead Vocal", "Backing Vocal", "Speech", "Drums" })
        CHECK(categories.count(c) == 1);
    for (const char* n : { "Backing Vocal - Airy", "Backing Vocal - Tight", "Kick Drum", "Snare Drum" })
        CHECK(findFactoryPreset(n) != nullptr);
    CHECK(findFactoryPreset("No Such Preset") == nullptr);

    // The natural lead vocal preset is exactly the engine defaults.
    CHECK(presets.front().settings == SmartGainSettings {});
}

TEST_CASE("Applying a preset keeps static gain and bypass", "[presets]")
{
    SmartGainSettings session;
    session.staticGainDb = 7.5f;
    session.bypass = true;
    session.riderStrength = 0.1f;

    const auto* tight = findFactoryPreset("Backing Vocal - Tight");
    REQUIRE(tight);
    const auto applied = applyPreset(session, tight->settings);
    CHECK(applied.staticGainDb == 7.5f);
    CHECK(applied.bypass);
    CHECK(applied.riderStrength == tight->settings.riderStrength);
    CHECK(applied.targetRmsDb == tight->settings.targetRmsDb);

    CHECK(matchesPreset(applied, tight->settings));
    auto edited = applied;
    edited.deadbandDb += 0.5f;
    CHECK_FALSE(matchesPreset(edited, tight->settings));
    auto onlyStaticChanged = applied;
    onlyStaticChanged.staticGainDb = -3.0f;
    CHECK(matchesPreset(onlyStaticChanged, tight->settings)); // static gain is not part of a preset
}

TEST_CASE("Airy backing vocal rides more gently than Tight", "[presets][behaviour]")
{
    // Same quiet-then-loud performance through both presets.
    const auto in = compose(kSr, { { Segment::Silence, 0.3 }, { Segment::Vocal, 6.0, -30.0 }, { Segment::Silence, 0.5 },
                                   { Segment::Vocal, 6.0, -11.0 } },
                            -68.0);
    auto run = [&](const char* name) {
        auto stage = makeStage(kSr, findFactoryPreset(name)->settings);
        std::vector<SmartGainTelemetry> ticks;
        processWithTelemetry(*stage, in, ticks);
        float up = 0.0f, down = 0.0f, fastestSlope = 0.0f;
        for (size_t i = 1; i < ticks.size(); ++i)
        {
            up = std::max(up, ticks[i].riderGainDb);
            down = std::min(down, ticks[i].riderGainDb);
            fastestSlope = std::max(fastestSlope, std::abs(ticks[i].riderGainDb - ticks[i - 1].riderGainDb) / 0.01f);
        }
        return std::array<float, 3> { up, down, fastestSlope };
    };
    const auto airy = run("Backing Vocal - Airy");
    const auto tight = run("Backing Vocal - Tight");
    INFO("airy up/down/slope " << airy[0] << " " << airy[1] << " " << airy[2]);
    INFO("tight up/down/slope " << tight[0] << " " << tight[1] << " " << tight[2]);
    CHECK(tight[0] > airy[0] + 1.0f);    // lifts quiet parts more
    CHECK(tight[1] < airy[1] - 1.0f);    // pulls loud parts down more
    CHECK(tight[2] > airy[2] * 1.5f);    // moves faster
    CHECK(airy[2] <= 4.0f * 1.001f);     // never faster than its own rate limit
}

TEST_CASE("Kick preset calibrates on a groove and never rides", "[presets][drums]")
{
    const auto in = kickGroove(kSr, 10.0, 110.0, 0.25f, -58.0, 21);
    auto stage = makeStage(kSr, findFactoryPreset("Kick Drum")->settings);
    stage->calibrator().requestStart(4.0);
    std::vector<SmartGainTelemetry> ticks;
    const auto out = processWithTelemetry(*stage, in, ticks);

    REQUIRE(stage->calibrator().state() == CalibrationState::Ready);
    const auto r = stage->calibrator().computeResult(-22.0f, -3.0f);
    INFO(r.summary);
    REQUIRE(r.valid);
    const double ref = referenceActiveRmsDb(in, kSr, -58.0);
    INFO("reference active RMS " << ref << " measured " << r.measuredMedianDb);
    CHECK(static_cast<double>(r.measuredMedianDb) == Approx(ref).margin(2.0));
    CHECK(r.recommendedGainDb == Approx(-22.0f - r.measuredMedianDb));

    for (const auto& t : ticks)
        REQUIRE(t.riderGainDb == 0.0f);
    CHECK(out == in); // rider off + static 0 dB: drums untouched (dynamics preserved)
}

TEST_CASE("Snare preset calibrates on backbeats", "[presets][drums]")
{
    const auto in = snareGroove(kSr, 12.0, 100.0, 0.3f, -58.0, 33);
    auto stage = makeStage(kSr, findFactoryPreset("Snare Drum")->settings);
    stage->calibrator().requestStart(3.0);
    std::vector<SmartGainTelemetry> ticks;
    processWithTelemetry(*stage, in, ticks);
    REQUIRE(stage->calibrator().state() == CalibrationState::Ready);
    const auto r = stage->calibrator().computeResult(-22.0f, -3.0f);
    INFO(r.summary);
    REQUIRE(r.valid);
    CHECK(static_cast<double>(r.measuredMedianDb) == Approx(referenceActiveRmsDb(in, kSr, -58.0)).margin(2.5));
}

TEST_CASE("Vocal default settings would not calibrate a sparse drum groove", "[presets][drums]")
{
    // Documents WHY drum presets exist: with vocal phrase-hold, separated hits
    // never form a continuous 400 ms "active" stretch.
    const auto in = snareGroove(kSr, 12.0, 100.0, 0.3f, -58.0, 33);
    auto stage = makeStage(kSr, SmartGainSettings {});
    stage->calibrator().requestStart(3.0);
    processMono(*stage, in, 480);
    CHECK(stage->calibrator().state() != CalibrationState::Ready);
}

TEST_CASE("Detector high-pass can be changed live without clicks or allocation-visible state jumps", "[presets][detector]")
{
    SmartGainSettings s;
    auto stage = makeStage(kSr, s);
    const auto in = compose(kSr, { { Segment::Vocal, 4.0, -24.0 } }, -65.0);
    std::vector<SmartGainTelemetry> ticks;
    int block = 0;
    processMono(*stage, in, hopFor(kSr), [&](size_t) {
        if (++block == 200)
        {
            s.detectorHpfHz = 30.0f;
            stage->setSettings(s);
        }
        SmartGainTelemetry t;
        if (stage->readTelemetry(t))
            ticks.push_back(t);
    });
    // Activity stays continuous across the change (no false phrase end).
    for (size_t i = tickAt(1.0); i < tickAt(4.0); ++i)
        REQUIRE(ticks[i].activity == ActivityState::Active);
}
