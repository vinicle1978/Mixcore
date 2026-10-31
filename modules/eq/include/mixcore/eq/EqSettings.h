#pragma once
// Parameter schema for the MixCore parametric EQ.
//
// Seven fixed bands, in signal order:
//   0 High-pass (12/24/48 dB/oct)   1 Low shelf   2 Bell 1   3 Bell 2
//   4 Bell 3                         5 High shelf  6 Low-pass (12/24/48 dB/oct)
// Bells and shelves are on by default at 0 dB (transparent); the filters are
// off. The EQ module itself is off by default.

#include <array>
#include <string>

namespace mixcore::eq {

enum class BandType
{
    HighPass,
    LowShelf,
    Bell,
    HighShelf,
    LowPass
};

inline constexpr int kNumBands = 7;

struct EqBand
{
    bool on = true;
    float freqHz = 1000.0f; ///< [20, 20000]
    float gainDb = 0.0f;    ///< [-18, 18] (ignored by the pass filters)
    float q = 0.707f;       ///< [0.1, 10] (shelves use at most 2; 24/48 dB filters are Butterworth)
    float slope = 24.0f;    ///< pass filters only: 12, 24 or 48 dB/oct

    bool operator==(const EqBand&) const = default;
};

struct EqSettings
{
    bool enabled = false;
    float outputDb = 0.0f; ///< [-18, 18]
    std::array<EqBand, kNumBands> bands = defaultBands();

    static BandType type(int band) noexcept;
    static const char* bandName(int band) noexcept;   ///< "High-pass", "Low shelf", "Bell 1", ...
    static std::array<EqBand, kNumBands> defaultBands() noexcept;

    void validate() noexcept;
    /// Number of bands that change the sound (on, and not a 0 dB bell/shelf).
    int activeBandCount() const noexcept;

    std::string serialise() const;
    static EqSettings deserialise(const std::string& text);

    bool operator==(const EqSettings&) const = default;
};

bool approximatelyEqual(const EqSettings& a, const EqSettings& b, float tolerance) noexcept;

} // namespace mixcore::eq
