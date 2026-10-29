// Regression test: a fixed synthetic vocal performance is processed with the
// default lead-vocal settings and summarised once per second (rider gain,
// output RMS, activity). The summary must match the approved reference within
// documented tolerances. To approve a deliberate behaviour change, run the
// test with MIXCORE_UPDATE_REFERENCE=1 and commit the new CSV with a note in
// docs/testing/test-plan.md explaining why.

#include "../support/StageHarness.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifndef MIXCORE_REGRESSION_DIR
#error "MIXCORE_REGRESSION_DIR must be defined"
#endif

using namespace mixcore;
using namespace mixcore::gain;
using namespace mixcore::test;

namespace {
struct Row
{
    int second;
    float riderGainDb;
    float outputRmsDb;
    int active;
};

std::vector<Row> render()
{
    const double sr = 48000.0;
    auto stage = makeStage(sr, SmartGainSettings {});
    auto in = compose(sr,
                      { { Segment::Silence, 1.0 },
                        { Segment::Vocal, 4.0, -24.0 },
                        { Segment::Silence, 0.8 },
                        { Segment::Vocal, 3.0, -13.0 },
                        { Segment::Silence, 1.2 },
                        { Segment::Vocal, 5.0, -30.0 },
                        { Segment::Silence, 3.0 },
                        { Segment::Vocal, 4.0, -19.0 } },
                      -62.0, 99);
    std::vector<SmartGainTelemetry> ticks;
    processWithTelemetry(*stage, in, ticks);
    std::vector<Row> rows;
    for (size_t i = 99; i < ticks.size(); i += 100)
    {
        const auto& t = ticks[i];
        rows.push_back({ static_cast<int>((i + 1) / 100), t.riderGainDb, t.outputRmsDb,
                         t.activity == ActivityState::Active ? 1 : 0 });
    }
    return rows;
}
} // namespace

TEST_CASE("Smart Gain lead-vocal regression matches the approved reference", "[regression]")
{
    const std::string path = std::string(MIXCORE_REGRESSION_DIR) + "/smart_gain_lead_vocal_v1.csv";
    const auto rows = render();

    const char* update = std::getenv("MIXCORE_UPDATE_REFERENCE");
    if (update && std::string(update) == "1")
    {
        std::ofstream out(path);
        out << "second,rider_gain_db,output_rms_db,active\n";
        out.setf(std::ios::fixed);
        out.precision(4);
        for (const auto& r : rows)
            out << r.second << ',' << r.riderGainDb << ',' << r.outputRmsDb << ',' << r.active << '\n';
        WARN("Reference updated: " << path);
        return;
    }

    std::ifstream in(path);
    REQUIRE(in.good()); // reference must exist; it is never auto-created
    std::string line;
    std::getline(in, line); // header
    size_t idx = 0;
    while (std::getline(in, line))
    {
        std::istringstream ls(line);
        Row ref {};
        char c;
        ls >> ref.second >> c >> ref.riderGainDb >> c >> ref.outputRmsDb >> c >> ref.active;
        REQUIRE(idx < rows.size());
        const auto& got = rows[idx++];
        INFO("second " << ref.second);
        CHECK(got.second == ref.second);
        CHECK(std::abs(got.riderGainDb - ref.riderGainDb) <= 0.02f);
        CHECK(std::abs(got.outputRmsDb - ref.outputRmsDb) <= 0.05f);
        CHECK(got.active == ref.active);
    }
    CHECK(idx == rows.size());
}
