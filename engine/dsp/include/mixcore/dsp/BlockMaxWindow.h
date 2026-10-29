#pragma once
// Maximum over the last N pushed values (typically per-control-tick peaks).
// N is small (tens to a few hundred), so a linear scan is cheaper and simpler
// than a monotonic deque.

#include <algorithm>
#include <vector>

namespace mixcore::dsp {

class BlockMaxWindow
{
public:
    void prepare(int numBlocks)
    {
        values_.assign(static_cast<size_t>(std::max(1, numBlocks)), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill(values_.begin(), values_.end(), 0.0f);
        index_ = 0;
    }

    void push(float v) noexcept
    {
        values_[index_] = v;
        index_ = (index_ + 1) % values_.size();
    }

    float max() const noexcept
    {
        float m = 0.0f;
        for (float v : values_)
            m = std::max(m, v);
        return m;
    }

private:
    std::vector<float> values_;
    size_t index_ = 0;
};

} // namespace mixcore::dsp
