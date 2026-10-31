#pragma once
// Comp Coach: "Listen & suggest" for the compressor.
//
// Reads the short-term levels (10 ms RMS and peak) entering the compressor for
// a few seconds of real playing (quiet entries are ignored), then works out
// how loud the loud and quiet parts are, how wide that range is and how peaky
// the source is. From that it proposes a threshold/ratio that gives a typical
// amount of reduction for this kind of source on the loud parts, a makeup
// that keeps the level about the same, and (when useful) an attack time.
// Plain statistics and the compressor's own static curve: no ML. Message
// thread only. Nothing is applied until the user presses Apply.

#include "mixcore/comp/CompSettings.h"
#include "mixcore/comp/LevelTap.h"
#include "mixcore/gain/SourceType.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mixcore::comp {

struct CompSuggestion
{
    std::string id;        ///< "threshold", "attack"
    std::string title;
    std::string why;
    CompSettings settings; ///< complete proposed settings (enabled == true)
    CompSettings base;     ///< the settings it was worked out from; Apply changes only what differs
};

struct LevelStats
{
    float loudDb = -150.0f;   ///< 90th percentile of the detector level
    float typicalDb = -150.0f;///< median
    float quietDb = -150.0f;  ///< 10th percentile
    float crestDb = 0.0f;     ///< loud peaks vs loud 50 ms RMS (90th percentiles)
    int entries = 0;
};

class CompListener
{
public:
    void start(double seconds, const LevelTap& tap) noexcept;
    void cancel() noexcept { listening_ = false; }
    bool listening() const noexcept { return listening_; }
    bool ready() const noexcept { return ready_; }
    float progress() const noexcept;

    /// Consume new entries from the tap (call regularly, e.g. 30 Hz).
    void pump(const LevelTap& tap);
    /// Feed one entry directly (tests).
    void feed(LevelEntry e);

    /// Statistics for the given detector type; valid once ready().
    LevelStats stats(Detector detector) const;
    /// At most 2 suggestions.
    std::vector<CompSuggestion> suggestions(gain::SourceType source, const CompSettings& current) const;

private:
    void finish();
    double seconds_ = 8.0;
    bool listening_ = false, ready_ = false;
    std::uint64_t read_ = 0;
    int active_ = 0;
    std::vector<LevelEntry> entries_;
};

/// Threshold that makes the static curve reduce `levelDb` by `reductionDb`.
float thresholdForReduction(float levelDb, float reductionDb, float ratio, float kneeDb) noexcept;

} // namespace mixcore::comp
