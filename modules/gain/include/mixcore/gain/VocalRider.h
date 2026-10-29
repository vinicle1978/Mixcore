#pragma once
// Adaptive vocal rider: slow, bounded, rate-limited gain that nudges the
// active vocal level towards the target. Distinct from static gain (never
// modifies it) and from compression (no per-transient gain reduction).
//
// Control rate (every tick):
//   if riding:   e = target - level            (level is post-static, pre-rider)
//                e' = 0                         if |e| <= deadband
//                e' = e - sign(e) * deadband    otherwise (continuous)
//                desired = clamp(strength * e', -maxDown, +maxUp)
//   else:        desired = current gain         (freeze: never boost in silence)
//                ...except after a long idle period, when positive gain decays
//                towards 0 dB so room noise between songs is not left boosted.
//                Negative gain is held (releasing it would raise the noise).
//   desired = min(desired, max(peakHeadroomDb, 0))   (preventative peak restriction)
//
// Audio rate (every sample):
//   one-pole towards desired (attack coef when falling, release when rising),
//   then slew-limited to maxRate dB/s. Coefficients are derived from seconds,
//   so the envelope is the same at 44.1, 48, 88.2 and 96 kHz.

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>

namespace mixcore::gain {

class VocalRider
{
public:
    struct Config
    {
        bool enabled = true;
        float targetDb = -18.0f;
        float strength = 0.5f;
        float rangeDb = 6.0f;
        float maxUpDb = 4.0f;
        float maxDownDb = 6.0f;
        float attackMs = 120.0f;
        float releaseMs = 500.0f;
        float maxRateDbPerSec = 6.0f;
        float deadbandDb = 1.5f;
    };

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        configure(config_);
        reset();
    }

    void reset() noexcept
    {
        gainDb_ = 0.0;
        desiredDb_ = 0.0f;
        restrictionDb_ = 0.0f;
    }

    void configure(const Config& c) noexcept
    {
        config_ = c;
        attackCoef_ = std::exp(-1.0 / (static_cast<double>(c.attackMs) * 0.001 * sampleRate_));
        releaseCoef_ = std::exp(-1.0 / (static_cast<double>(c.releaseMs) * 0.001 * sampleRate_));
        maxStepDb_ = static_cast<double>(c.maxRateDbPerSec) / sampleRate_;
    }

    /// Control-rate update.
    /// @param riding         true only while the vocal is confirmed Active.
    /// @param levelDb        robust active level, post-static / pre-rider.
    /// @param peakHeadroomDb restrictThreshold - predicted pre-rider peak.
    /// @param idleDecayDb    when > 0 (long inactivity), how far positive gain
    ///                       may fall this tick.
    void updateTarget(bool riding, float levelDb, float peakHeadroomDb, float idleDecayDb = 0.0f) noexcept
    {
        float unrestricted;
        if (!config_.enabled)
        {
            unrestricted = 0.0f; // glide back to unity when disabled
        }
        else if (riding && std::isfinite(levelDb))
        {
            const float e = config_.targetDb - levelDb;
            const float magnitude = std::abs(e);
            const float shaped = magnitude <= config_.deadbandDb
                                     ? 0.0f
                                     : std::copysign(magnitude - config_.deadbandDb, e);
            const float up = std::min(config_.maxUpDb, config_.rangeDb);
            const float down = std::min(config_.maxDownDb, config_.rangeDb);
            unrestricted = std::clamp(config_.strength * shaped, -down, up);
        }
        else
        {
            unrestricted = static_cast<float>(gainDb_); // freeze where we are
            // Idle release walks the *target* down at a fixed rate (anchored to
            // the previous target, which the frozen gain has already reached),
            // so the decay rate does not depend on the smoothing time constant.
            if (idleDecayDb > 0.0f && desiredDb_ > 0.0f)
                unrestricted = std::max(0.0f, std::min(desiredDb_, unrestricted) - idleDecayDb);
        }

        const float cap = std::isfinite(peakHeadroomDb) ? std::max(peakHeadroomDb, 0.0f) : 0.0f;
        desiredDb_ = std::min(unrestricted, cap);
        restrictionDb_ = std::max(0.0f, unrestricted - desiredDb_);
    }

    /// Audio-rate update; returns the rider gain for this sample in dB.
    /// State is kept in double: per-sample steps (~1e-4 dB) added to a float
    /// gain of several dB would lose ~1 % of each step to rounding.
    double nextGainDb() noexcept
    {
        const double diff = static_cast<double>(desiredDb_) - gainDb_;
        if (std::abs(diff) > 0.0)
        {
            const double coef = diff < 0.0 ? attackCoef_ : releaseCoef_;
            const double step = std::clamp(diff * (1.0 - coef), -maxStepDb_, maxStepDb_);
            gainDb_ += step;
            if (std::abs(static_cast<double>(desiredDb_) - gainDb_) < 1.0e-9)
                gainDb_ = static_cast<double>(desiredDb_);
        }
        return gainDb_;
    }

    float gainDb() const noexcept { return static_cast<float>(gainDb_); }
    float desiredDb() const noexcept { return desiredDb_; }
    float restrictionDb() const noexcept { return restrictionDb_; }
    const Config& config() const noexcept { return config_; }

private:
    Config config_;
    double sampleRate_ = 48000.0;
    double attackCoef_ = 0.0;
    double releaseCoef_ = 0.0;
    double maxStepDb_ = 0.0;
    double gainDb_ = 0.0;
    float desiredDb_ = 0.0f;
    float restrictionDb_ = 0.0f;
};

} // namespace mixcore::gain
