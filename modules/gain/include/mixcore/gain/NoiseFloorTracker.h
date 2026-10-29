#pragma once
// Noise-floor estimate from control-rate detector levels (dB).
//
// Minimum statistics: per-100 ms block minima are kept for a rolling window
// (default 5 s); the floor is the minimum over that window. The estimate falls
// instantly and rises at a bounded rate, and is clamped to a ceiling so that a
// long sustained note cannot be mistaken for "noise" and disable the rider
// entirely (see docs/dsp/smart-gain-stage.md, "Noise floor").

#include <algorithm>
#include <vector>

namespace mixcore::gain {

class NoiseFloorTracker
{
public:
    void prepare(int ticksPerBlock, int numBlocks, double tickSeconds)
    {
        ticksPerBlock_ = std::max(1, ticksPerBlock);
        blocks_.assign(static_cast<size_t>(std::max(1, numBlocks)), 0.0f);
        tickSeconds_ = tickSeconds;
        reset();
    }

    void reset() noexcept
    {
        filled_ = 0;
        index_ = 0;
        ticksInBlock_ = 0;
        blockMin_ = 0.0f;
        smoothed_ = minDb;
        initialised_ = false;
    }

    /// Call once per control tick with the detector level in dBFS.
    void update(float levelDb) noexcept
    {
        blockMin_ = (ticksInBlock_ == 0) ? levelDb : std::min(blockMin_, levelDb);
        if (++ticksInBlock_ >= ticksPerBlock_)
        {
            blocks_[index_] = blockMin_;
            index_ = (index_ + 1) % blocks_.size();
            filled_ = std::min(filled_ + 1, blocks_.size());
            ticksInBlock_ = 0;
        }

        float raw = (ticksInBlock_ > 0) ? blockMin_ : 1.0e9f;
        for (size_t i = 0; i < filled_; ++i)
            raw = std::min(raw, blocks_[i]);
        raw = std::clamp(raw, minDb, ceilingDb);

        if (!initialised_ || raw < smoothed_)
            smoothed_ = raw;
        else
            smoothed_ = std::min(raw, smoothed_ + riseDbPerSec * static_cast<float>(tickSeconds_));
        initialised_ = true;
    }

    float floorDb() const noexcept { return smoothed_; }

    float ceilingDb = -40.0f;   ///< Floor never exceeds this (dBFS).
    float minDb = -100.0f;      ///< Floor never goes below this (dBFS).
    float riseDbPerSec = 6.0f;  ///< Upward slew of the estimate.

private:
    std::vector<float> blocks_;
    size_t filled_ = 0;
    size_t index_ = 0;
    int ticksPerBlock_ = 10;
    int ticksInBlock_ = 0;
    float blockMin_ = 0.0f;
    float smoothed_ = -100.0f;
    double tickSeconds_ = 0.01;
    bool initialised_ = false;
};

} // namespace mixcore::gain
