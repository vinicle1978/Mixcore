#pragma once
// 4x oversampling with two linear-phase halfband FIR stages (Kaiser-windowed,
// designed at start-up, no tables copied from anywhere). Integer latency:
//   stage 1 (47 taps at 2x) + stage 2 (21 taps at 4x) = 23 + 5 = 28 samples.
// Realtime-safe: fixed-size state, no allocation.

#include <array>

namespace mixcore::sat {

class HalfbandStage
{
public:
    static constexpr int kMaxTaps = 47;
    void design(int taps, double beta) noexcept;
    void reset() noexcept;
    /// One input sample -> two output samples at twice the rate.
    void up(float x, float& out0, float& out1) noexcept;
    /// Two input samples at twice the rate -> one output sample.
    float down(float in0, float in1) noexcept;
    /// Latency of one up + down pair, in samples at the lower rate.
    int latency() const noexcept { return (taps_ - 1) / 2; }

private:
    std::array<double, kMaxTaps> h_ {};
    int taps_ = 0;
    std::array<float, 2 * kMaxTaps> upHist_ {}, downHist_ {};
    int upPos_ = 0, downPos_ = 0;
};

class Oversampler4x
{
public:
    static constexpr int kFactor = 4;
    Oversampler4x();
    void reset() noexcept;
    void up(float x, float* out4) noexcept;
    float down(const float* in4) noexcept;
    static constexpr int latencySamples() noexcept { return 28; }

private:
    HalfbandStage s1_; // 1x <-> 2x
    HalfbandStage s2_; // 2x <-> 4x
};

} // namespace mixcore::sat
