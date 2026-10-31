#pragma once
// Lock-free record of short-term levels (one entry per 10 ms hop) entering the
// compressor, for the Comp Coach. Audio thread pushes, message thread reads
// new entries since its last read. Relaxed atomics: no data race, no locks.

#include <array>
#include <atomic>
#include <cstdint>

namespace mixcore::comp {

struct LevelEntry
{
    float rmsDb = -150.0f;
    float peakDb = -150.0f;
};

class LevelTap
{
public:
    static constexpr int kSize = 8192; // ~82 s at 10 ms per entry

    void push(float rmsDb, float peakDb) noexcept
    {
        const auto w = write_.load(std::memory_order_relaxed);
        rms_[w & (kSize - 1)].store(rmsDb, std::memory_order_relaxed);
        peak_[w & (kSize - 1)].store(peakDb, std::memory_order_relaxed);
        write_.store(w + 1, std::memory_order_release);
    }

    std::uint64_t written() const noexcept { return write_.load(std::memory_order_acquire); }

    /// Entry number `index` (must be within the last kSize entries).
    LevelEntry at(std::uint64_t index) const noexcept
    {
        return { rms_[index & (kSize - 1)].load(std::memory_order_relaxed), peak_[index & (kSize - 1)].load(std::memory_order_relaxed) };
    }

private:
    std::array<std::atomic<float>, kSize> rms_ {}, peak_ {};
    std::atomic<std::uint64_t> write_ { 0 };
};

} // namespace mixcore::comp
