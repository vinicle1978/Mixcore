#pragma once
// Single-producer / single-consumer "latest value" exchange.
//
// The audio thread writes complete snapshots; the UI thread reads the most
// recent complete snapshot. Neither side ever blocks or allocates, and the
// reader never sees a half-written value (unlike a struct of independent
// atomics). Older snapshots are dropped, which is exactly what meters want.

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace mixcore {

template <typename T>
class TripleBuffer
{
    static_assert(std::is_trivially_copyable_v<T>, "TripleBuffer requires a trivially copyable type");

public:
    TripleBuffer() = default;
    TripleBuffer(const TripleBuffer&) = delete;
    TripleBuffer& operator=(const TripleBuffer&) = delete;

    /// Producer side (audio thread). Wait-free.
    void write(const T& value) noexcept
    {
        slots_[writeIndex_] = value;
        const auto previous = middle_.exchange(static_cast<std::uint8_t>(writeIndex_ | kDirtyBit),
                                               std::memory_order_acq_rel);
        writeIndex_ = previous & kIndexMask;
    }

    /// Consumer side (UI / message thread). Wait-free.
    /// Returns true when `out` holds a snapshot newer than the previous read.
    bool read(T& out) noexcept
    {
        bool fresh = false;
        if ((middle_.load(std::memory_order_relaxed) & kDirtyBit) != 0)
        {
            const auto previous = middle_.exchange(readIndex_, std::memory_order_acq_rel);
            readIndex_ = previous & kIndexMask;
            fresh = true;
        }
        out = slots_[readIndex_];
        return fresh;
    }

private:
    static constexpr std::uint8_t kDirtyBit = 0x4;
    static constexpr std::uint8_t kIndexMask = 0x3;

    T slots_[3] {};
    std::atomic<std::uint8_t> middle_ { 1 };
    std::uint8_t writeIndex_ = 0; // owned by producer
    std::uint8_t readIndex_ = 2;  // owned by consumer
};

} // namespace mixcore
