#pragma once
// Parameter schema for the Smart Gain Stage.
//
// All values are user-configurable. Defaults are the "lead vocal, moderate"
// starting profile from the product spec; they are product defaults, not
// universal standards. Ranges are enforced by validate(), which also replaces
// non-finite values with defaults, so the DSP never sees out-of-range input.

#include <string>

namespace mixcore::gain {

struct SmartGainSettings
{
    // --- Targets & static gain -------------------------------------------
    float targetRmsDb = -18.0f;       ///< dBFS RMS (full-scale sine = -3.01). Range [-40, -6].
    float staticGainDb = 0.0f;        ///< Static (calibrated or manual) gain. Range [-18, +18].

    // --- Adaptive rider ---------------------------------------------------
    bool riderEnabled = true;
    float riderStrength = 0.5f;       ///< Fraction of the (deadband-shaped) error corrected. [0, 1].
    float riderRangeDb = 6.0f;        ///< Overall symmetric limit on rider gain. [0, 12].
    float riderMaxUpDb = 4.0f;        ///< Maximum upward correction. [0, 12].
    float riderMaxDownDb = 6.0f;      ///< Maximum downward correction (positive number). [0, 12].
    float riderAttackMs = 120.0f;     ///< Smoothing when gain is reduced. [10, 2000].
    float riderReleaseMs = 500.0f;    ///< Smoothing when gain is increased. [10, 5000].
    float riderMaxRateDbPerSec = 6.0f;///< Gain slew limit. [0.5, 60].
    float deadbandDb = 1.5f;          ///< Errors within +/- deadband are ignored. [0, 6].
    float idleReleaseDelayMs = 2000.0f; ///< After this much inactivity, positive rider gain decays. [250, 10000].
    float idleReleaseDbPerSec = 1.0f; ///< Decay rate towards 0 dB; 0 = hold forever. [0, 6].

    // --- Activity detection ----------------------------------------------
    float activityConfirmMs = 80.0f;  ///< Signal must stay above threshold this long. [0, 500].
    float phraseHoldMs = 150.0f;      ///< Hold after level drops before "inactive". [0, 2000].
    float activityMarginDb = 10.0f;   ///< Open threshold above estimated noise floor. [3, 30].
    float detectorHpfHz = 100.0f;     ///< Detector high-pass: ignores rumble below this. [20, 400].

    // --- Peak safety ------------------------------------------------------
    float peakWarningDb = -6.0f;      ///< Output sample-peak warning. [-24, 0].
    float peakRestrictDb = -3.0f;     ///< Rider may not raise predicted peaks above this. [-24, 0].
    bool peakGuardEnabled = false;    ///< Optional lookahead guard (adds latency).
    float peakGuardCeilingDb = -3.0f; ///< Guard ceiling, dBFS sample peak. [-24, 0].

    // --- Global -----------------------------------------------------------
    bool bypass = false;

    /// Clamp every field into its documented range; non-finite -> default.
    void validate() noexcept;

    /// Text serialisation ("key=value;" pairs). Unknown keys are ignored and
    /// missing keys keep their defaults, so older/newer sessions load safely.
    std::string serialise() const;
    static SmartGainSettings deserialise(const std::string& text);

    bool operator==(const SmartGainSettings&) const = default;
};

} // namespace mixcore::gain
