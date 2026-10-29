#pragma once
// Parameter schema for the MixCore gate / expander.
//
// One module covers both behaviours:
//   ratio >= kHardGateRatio  -> gate: below threshold the signal is attenuated
//                               by the full range.
//   ratio <  kHardGateRatio  -> downward expander: below threshold the
//                               attenuation grows with the distance below
//                               threshold, (threshold - level) * (ratio - 1),
//                               capped at the range. Gentler; best for vocals.
//
// Levels are dBFS of the (side-chain filtered) detector signal, measured AFTER
// the Smart Gain Stage, i.e. after static gain and rider.

#include <string>

namespace mixcore::gate {

inline constexpr float kHardGateRatio = 20.0f;

struct GateSettings
{
    // Defaults = the "Vocal - Gentle Expander" profile, switched OFF. A gate
    // must be fitted to the source before use, and if it is switched on
    // without fitting, a gentle 10 dB expander is the least harmful choice.
    bool enabled = false;

    float thresholdDb = -50.0f;    ///< Opens at/above this detector level. [-80, 0].
    float rangeDb = 10.0f;         ///< Maximum attenuation when closed (positive dB). [0, 80].
    float ratio = 2.0f;            ///< Expansion ratio; >= 20 behaves as a hard gate. [1.2, 20].
    float hysteresisDb = 6.0f;     ///< Closes at threshold - hysteresis (prevents chatter). [0, 20].

    float attackMs = 5.0f;         ///< Time to open fully (linear dB ramp over the range). [0.05, 50].
    float holdMs = 200.0f;         ///< Stays open this long after the level drops. [0, 2000].
    float releaseMs = 400.0f;      ///< Time to close fully (linear dB ramp over the range). [5, 4000].

    float sidechainHpfHz = 120.0f; ///< Key high-pass (20 = off). [20, 2000].
    float sidechainLpfHz = 10000.0f; ///< Key low-pass (20000 = off). [500, 20000].
    bool rmsDetector = true;       ///< true: 10 ms RMS (voices); false: fast peak detector (drums).

    bool transientTrigger = false; ///< Only OPEN on a sharp rise (rejects cymbal wash / sustained bleed).
    float transientDb = 6.0f;      ///< Rise needed over the recent level. [1, 24].

    float lookaheadMs = 0.0f;      ///< Opens this much before the hit; adds the same latency. [0, 5].

    void validate() noexcept;
    bool isHardGate() const noexcept { return ratio >= kHardGateRatio; }

    std::string serialise() const;
    static GateSettings deserialise(const std::string& text);

    bool operator==(const GateSettings&) const = default;
};

} // namespace mixcore::gate
