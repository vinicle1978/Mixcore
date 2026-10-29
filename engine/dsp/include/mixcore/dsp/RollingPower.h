#pragma once
// Exact sliding-window mean power (rectangular window) with O(1) cost per
// sample. Used for RMS metering and level detection.
//
// The running sum is recomputed from the stored values once per window wrap,
// which bounds floating-point drift (important when a loud signal is followed
// by silence) at an amortised cost of one extra addition per sample.

#include <algorithm>
#include <numeric>
#include <vector>

namespace mixcore::dsp {

class RollingPower
{
public:
    /// Allocates. Call from a non-realtime thread.
    void prepare(int windowSamples)
    {
        window_ = std::max(1, windowSamples);
        buffer_.assign(static_cast<size_t>(window_), 0.0);
        reset();
    }

    void reset() noexcept
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.0);
        sum_ = 0.0;
        index_ = 0;
        filled_ = 0;
    }

    void push(double power) noexcept
    {
        sum_ += power - buffer_[static_cast<size_t>(index_)];
        buffer_[static_cast<size_t>(index_)] = power;
        if (++index_ == window_)
        {
            index_ = 0;
            sum_ = std::accumulate(buffer_.begin(), buffer_.end(), 0.0);
        }
        if (filled_ < window_)
            ++filled_;
    }

    /// Mean power over the samples seen so far (the whole window once filled).
    double meanPower() const noexcept
    {
        return filled_ > 0 ? std::max(0.0, sum_) / static_cast<double>(filled_) : 0.0;
    }

    bool isFilled() const noexcept { return filled_ >= window_; }
    int windowSamples() const noexcept { return window_; }

private:
    std::vector<double> buffer_;
    double sum_ = 0.0;
    int window_ = 1;
    int index_ = 0;
    int filled_ = 0;
};

} // namespace mixcore::dsp
