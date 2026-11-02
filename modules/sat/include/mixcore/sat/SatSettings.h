#pragma once
// Parameter schema for the MixCore saturation module.

#include <string>

namespace mixcore::sat {

enum class SatType
{
    Tape = 0,    ///< smooth symmetric soft clip (tanh): odd harmonics, rounded peaks
    Tube = 1,    ///< asymmetric soft clip: adds even harmonics (2nd, 4th), "warmth"
    Console = 2, ///< gentle cubic curve: subtle, mostly 3rd harmonic until it flattens
    Clip = 3     ///< hard clip: bright and aggressive, flat tops
};
inline constexpr int kNumSatTypes = 4;
const char* satTypeName(SatType t) noexcept;

enum class SatQuality
{
    Live = 0,  ///< no oversampling, zero latency (some aliasing at high drive)
    Studio = 1 ///< 4x oversampling, cleaner at high drive, adds latency (reported)
};

struct SatSettings
{
    bool enabled = false;
    SatType type = SatType::Tape;
    float driveDb = 6.0f;       ///< [0, 36]
    float mixPercent = 100.0f;  ///< [0, 100] parallel: 0 = dry
    float outputDb = 0.0f;      ///< [-24, 12]
    bool autoGain = true;       ///< keep the loudness about the same as the drive changes
    SatQuality quality = SatQuality::Live;

    void validate() noexcept;
    std::string serialise() const;
    static SatSettings deserialise(const std::string& text);
    bool operator==(const SatSettings&) const = default;
};

bool approximatelyEqual(const SatSettings& a, const SatSettings& b, float tolerance) noexcept;

} // namespace mixcore::sat
