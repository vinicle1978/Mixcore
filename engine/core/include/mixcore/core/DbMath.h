#pragma once
// Level-conversion helpers shared by every MixCore module.
//
// Units used throughout MixCore:
//   * Linear gain / sample values: dimensionless, 1.0 == 0 dBFS.
//   * Power: mean of squared sample values (1.0 == full-scale DC; a full-scale
//     sine has power 0.5, i.e. -3.01 dBFS RMS). MixCore does NOT apply the
//     +3 dB "AES sine" offset some meters use. This is documented in
//     docs/dsp/smart-gain-stage.md.
//   * Levels: dBFS (RMS or sample peak), dBTP for true-peak estimates.

#include <algorithm>
#include <cmath>

namespace mixcore {

/// Value reported for silence / undefined levels. Always finite so it is safe
/// to display and to compare against.
inline constexpr float kSilenceDb = -150.0f;

inline float dbToGain(float db) noexcept
{
    // ln(10) / 20
    return std::exp(db * 0.11512925464970228f);
}

/// Double-precision variant, used where gain is accumulated per sample.
inline double dbToGainD(double db) noexcept
{
    return std::exp(db * 0.11512925464970229);
}

inline float gainToDb(float gain) noexcept
{
    return gain > 3.16227766e-8f ? 20.0f * std::log10(gain) : kSilenceDb;
}

inline float powerToDb(double power) noexcept
{
    return power > 1.0e-15 ? static_cast<float>(10.0 * std::log10(power)) : kSilenceDb;
}

inline double dbToPower(double db) noexcept
{
    return std::pow(10.0, db / 10.0);
}

/// Clamp a value to [lo, hi]; non-finite input returns `fallback`.
inline float clampFinite(float v, float lo, float hi, float fallback) noexcept
{
    if (!std::isfinite(v))
        return fallback;
    return std::clamp(v, lo, hi);
}

/// One-pole smoothing coefficient for a time constant in seconds at `rate` Hz.
/// y += (1 - coef) * (x - y). Returns 0 (instant) for non-positive times.
inline float timeConstantCoef(double seconds, double rate) noexcept
{
    if (seconds <= 0.0 || rate <= 0.0)
        return 0.0f;
    return static_cast<float>(std::exp(-1.0 / (seconds * rate)));
}

inline int msToSamples(double ms, double sampleRate) noexcept
{
    return std::max(1, static_cast<int>(std::lround(ms * 0.001 * sampleRate)));
}

} // namespace mixcore
