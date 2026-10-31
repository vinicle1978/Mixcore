#pragma once
// Parameter schema for the MixCore compressor.
//
// Levels are dBFS on MixCore's scale (full-scale sine RMS = -3 dBFS). Smart
// Gain brings the channel to its target RMS (default -18 dBFS) before the
// compressor, so profile thresholds are written relative to that level.

#include <string>

namespace mixcore::comp {

enum class Detector
{
    Peak = 0, ///< sample peak: catches transients (drums)
    Rms = 1   ///< 10 ms RMS: follows loudness (voices, instruments)
};

struct CompSettings
{
    bool enabled = false;
    float thresholdDb = -18.0f; ///< [-60, 0]
    float ratio = 3.0f;         ///< [1, 20]  (20 = limiting)
    float kneeDb = 6.0f;        ///< [0, 24]  soft-knee width
    float attackMs = 10.0f;     ///< [0.1, 100]
    float releaseMs = 150.0f;   ///< [10, 2000] (with auto release: the fast part)
    bool autoRelease = false;   ///< program-dependent release
    float makeupDb = 0.0f;      ///< [-12, 24] (ignored while autoMakeup is on)
    bool autoMakeup = false;    ///< makeup follows the average reduction (slow)
    float mixPercent = 100.0f;  ///< [0, 100] parallel compression: 0 = dry
    bool scHpfOn = true;        ///< sidechain high-pass (low end doesn't drive the compressor)
    float scHpfHz = 80.0f;      ///< [20, 500]
    Detector detector = Detector::Rms;

    void validate() noexcept;
    std::string serialise() const;
    static CompSettings deserialise(const std::string& text);

    bool operator==(const CompSettings&) const = default;
};

bool approximatelyEqual(const CompSettings& a, const CompSettings& b, float tolerance) noexcept;

/// Static gain computer (soft knee, log domain): gain change in dB (<= 0) for
/// a detector level `levelDb`. Used by the DSP, the UI transfer curve and the
/// Coach, so all three always agree.
float gainChangeDb(float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept;

} // namespace mixcore::comp
