#pragma once
// Lock-free tap of the most recent audio for the spectrum analyzer and the EQ
// Coach. The audio thread pushes samples (wait-free, no allocation); the UI
// thread copies the newest N samples. Stored as relaxed atomics, so a read
// that overlaps a write sees either old or new samples (fine for an analyzer)
// and there is no data race.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace mixcore::eq {

class SpectrumTap
{
public:
    static constexpr int kSize = 16384; // power of two, > 3 x the largest read

    /// Audio thread.
    void push(float v) noexcept
    {
        const auto w = write_.load(std::memory_order_relaxed);
        buf_[w & (kSize - 1)].store(v, std::memory_order_relaxed);
        write_.store(w + 1, std::memory_order_release);
    }

    /// UI thread: copies the newest `n` samples (n <= kSize / 2) into `out`.
    /// Returns the total number of samples ever pushed (to detect new data).
    std::uint64_t readLatest(float* out, int n) const noexcept
    {
        n = std::clamp(n, 0, kSize / 2);
        const auto w = write_.load(std::memory_order_acquire);
        const auto start = w >= static_cast<std::uint64_t>(n) ? w - static_cast<std::uint64_t>(n) : 0;
        int i = 0;
        for (auto k = start; k < w; ++k, ++i)
            out[i] = buf_[k & (kSize - 1)].load(std::memory_order_relaxed);
        for (; i < n; ++i)
            out[i] = 0.0f;
        return w;
    }

    std::uint64_t written() const noexcept { return write_.load(std::memory_order_acquire); }

private:
    std::array<std::atomic<float>, kSize> buf_ {};
    std::atomic<std::uint64_t> write_ { 0 };
};

} // namespace mixcore::eq
