#pragma once
// Parameter schema for the MixCore drum module.
//
// The module has a master power switch (moduleOn) and two processes, each with
// its own on/off: de-bleed (hit-correlated high-band bleed suppression) and
// replacement (hit detection + sample playback, mixed with the original).
// Unknown/missing keys fall back to defaults, so older files stay loadable.

#include <string>

namespace mixcore::drum {

struct DrumSettings
{
    /// Master power for the whole drum module. Off = nothing in it processes.
    bool moduleOn = false;

    // ---- De-bleed ---------------------------------------------------------
    bool debleedEnabled = false;
    float debleedAmount = 50.0f;      ///< 0..100 %: maximum high-band reduction = amount% of 30 dB. [0, 100].
    float debleedCrossoverHz = 5000.0f; ///< Above this, energy the drum itself doesn't explain is bleed. [1000, 12000].
    float debleedLengthMs = 150.0f;   ///< How long the drum's own high end may ring after a hit (snare wires). [10, 1000].
    float debleedToleranceDb = 3.0f;  ///< High end may exceed the learned drum level by this before reduction. [0, 12].

    // ---- Replacement / augmentation ----------------------------------------
    bool replaceEnabled = false;
    float replaceThresholdDb = -24.0f;   ///< Detector level (dBFS) that counts as a hit. [-60, 0].
    float replaceRetriggerMs = 40.0f;    ///< Minimum time between two hits (rejects double triggers). [5, 500].
    float replaceDynamics = 100.0f;      ///< 0 % = every hit at full velocity; 100 % = follow the playing. [0, 100].
    float replaceRangeDb = 24.0f;        ///< Input range above the threshold mapped to velocity 0..1. [6, 48].
    /// Blend between the original and the samples: 0 % = original only,
    /// 50 % = both at full level (augmentation), 100 % = samples only (replacement). [0, 100].
    float replaceMix = 50.0f;
    float replaceSampleDb = 0.0f;        ///< Level of the triggered samples (trim). [-40, 12].
    float replaceOriginalDb = 0.0f;      ///< Level of the original drum; -60 = off (pure replacement). [-60, 12].
    float replacePitchSt = 0.0f;         ///< Sample pitch, semitones. [-12, 12].
    float replaceAttackMs = 0.0f;        ///< Fade-in at the start of each sample (softens the transient). [0, 30].
    float replaceLengthMs = 2000.0f;     ///< Sample length; 2000 = full sample. [20, 2000].
    float replaceLookaheadMs = 1.5f;     ///< Detection window (= added latency). Live 1.5 ms, studio up to 10. [0.5, 10].
    float replaceDetectLowHz = 20.0f;    ///< Detector high-pass (ignore content below). [20, 2000].
    float replaceDetectHighHz = 20000.0f;///< Detector low-pass (ignore content above). [100, 20000].

    void validate() noexcept;
    bool debleedActive() const noexcept { return moduleOn && debleedEnabled; }
    bool replaceActive() const noexcept { return moduleOn && replaceEnabled; }
    float maxReductionDb() const noexcept { return 0.3f * debleedAmount; }

    std::string serialise() const;
    static DrumSettings deserialise(const std::string& text);

    bool operator==(const DrumSettings&) const = default;
};

/// Every field equal; floats within `tolerance` (relative for values > 1).
bool approximatelyEqual(const DrumSettings& a, const DrumSettings& b, float tolerance) noexcept;

} // namespace mixcore::drum
