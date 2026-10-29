#pragma once
// Snapshot published by the audio thread once per control tick (10 ms) via a
// TripleBuffer. Trivially copyable; the UI never touches live DSP state.
//
// Peak fields are the maximum over the last 50 ms (5 ticks), so a reader
// polling at 20 Hz or faster never misses a peak; UI meters apply their own
// decay / hold. Levels are dBFS; kSilenceDb (-150) means silence.

#include <cstdint>

namespace mixcore::gain {

enum class ActivityState : std::uint8_t
{
    Inactive = 0,   ///< No vocal detected; rider frozen.
    Confirming = 1, ///< Above threshold, waiting for confirmation time.
    Active = 2,     ///< Vocal active; rider tracking.
    Hold = 3        ///< Phrase end: level dropped, rider frozen during hold.
};

enum class CalibrationState : std::uint8_t
{
    Idle = 0,
    Requested = 1,
    Collecting = 2,
    Ready = 3,
    TimedOut = 4
};

struct SmartGainTelemetry
{
    std::uint64_t sequence = 0;

    // Levels
    float inputRmsDb = -150.0f;          ///< 400 ms window, raw input.
    float outputRmsDb = -150.0f;         ///< 400 ms window, output.
    float inputPeakDb = -150.0f;         ///< Sample peak, last 50 ms.
    float outputPeakDb = -150.0f;        ///< Sample peak, last 50 ms.
    float outputTruePeakDb = -150.0f;    ///< 4x oversampled estimate (dBTP), last 50 ms.
    float noiseFloorDb = -150.0f;        ///< Detector (high-passed) noise-floor estimate.

    // Gains
    float staticGainDb = 0.0f;           ///< Static gain currently applied.
    float riderGainDb = 0.0f;            ///< Rider gain currently applied.
    float riderTargetDb = 0.0f;          ///< Gain the rider is moving towards.
    float riderRestrictionDb = 0.0f;     ///< Upward gain withheld by peak restriction (>= 0).
    float peakGuardReductionDb = 0.0f;   ///< Max guard gain reduction this tick (>= 0).

    // Rider analysis
    float levelEstimateDb = -150.0f;     ///< Robust active level, post-static, pre-rider.
    float targetErrorDb = 0.0f;          ///< target - estimated output level (when valid).
    bool targetErrorValid = false;

    // States & flags
    ActivityState activity = ActivityState::Inactive;
    bool riderEnabled = true;
    bool inputClipping = false;          ///< Held ~2 s after the last clip event.
    std::uint32_t inputClipEvents = 0;   ///< Total since reset.
    bool peakWarning = false;            ///< Output sample peak above warning (held ~1 s).
    bool peakRestrictionActive = false;  ///< Rider upward gain currently restricted.
    bool peakGuardEnabled = false;
    bool peakGuardActive = false;        ///< Guard reduced gain this tick.
    bool bypassed = false;
    std::uint32_t nonFiniteInputSamples = 0; ///< NaN/Inf input samples replaced by 0.

    // Calibration
    CalibrationState calibration = CalibrationState::Idle;
    float calibrationProgress = 0.0f;    ///< 0..1

    int latencySamples = 0;
    double sampleRate = 0.0;
};

} // namespace mixcore::gain
