#pragma once
// Parameter schema for the MixCore drum module.
//
// Phase 1: de-bleed (hit-correlated high-band bleed suppression).
// Phase 2 (planned): drum replacement / augmentation with the user's own WAV
// kits and a starter kit. Those fields will be added here; files saved now
// stay loadable because unknown/missing keys fall back to defaults.

#include <string>

namespace mixcore::drum {

struct DrumSettings
{
    // ---- De-bleed ---------------------------------------------------------
    bool debleedEnabled = false;
    float debleedAmount = 50.0f;      ///< 0..100 %: maximum high-band reduction = amount% of 30 dB. [0, 100].
    float debleedCrossoverHz = 5000.0f; ///< Above this, energy the drum itself doesn't explain is bleed. [1000, 12000].
    float debleedLengthMs = 150.0f;   ///< How long the drum's own high end may ring after a hit (snare wires). [10, 1000].
    float debleedToleranceDb = 3.0f;  ///< High end may exceed the learned drum level by this before reduction. [0, 12].

    void validate() noexcept;
    float maxReductionDb() const noexcept { return 0.3f * debleedAmount; }

    std::string serialise() const;
    static DrumSettings deserialise(const std::string& text);

    bool operator==(const DrumSettings&) const = default;
};

} // namespace mixcore::drum
