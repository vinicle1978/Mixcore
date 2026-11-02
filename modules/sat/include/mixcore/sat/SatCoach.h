#pragma once
// Sat Coach: "Listen & suggest" for saturation. Uses the compressor's level
// listener (comp::CompListener) on the levels entering the saturation module,
// then picks the drive at which the chosen curve adds a typical amount of
// harmonics for this kind of source on the loud peaks (computed from the curve
// itself, see SatCurves). No ML. Nothing is applied until the user presses Apply.

#include "mixcore/comp/CompCoach.h"
#include "mixcore/gain/SourceType.h"
#include "mixcore/sat/SatSettings.h"

#include <string>
#include <vector>

namespace mixcore::sat {

struct SatSuggestion
{
    std::string id; ///< "drive", "quality", "type"
    std::string title;
    std::string why;
    SatSettings settings; ///< proposed (enabled == true)
    SatSettings base;     ///< what it was worked out from; Apply changes only what differs
};

/// Typical harmonic level on loud peaks for a source (dB relative to the fundamental).
float targetThdDb(gain::SourceType source) noexcept;

/// `stats` must come from a listener reading peak levels (Detector::Peak).
std::vector<SatSuggestion> satSuggestions(const comp::LevelStats& stats, gain::SourceType source, const SatSettings& current);

} // namespace mixcore::sat
