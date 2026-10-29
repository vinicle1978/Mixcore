#pragma once
// Optional lookahead peak guard (a transparent brick-wall limiter for safety,
// not a loudness tool).
//
// Algorithm (stereo-linked, sample-peak):
//   r[t]  = min(1, ceiling / max_ch |x_ch[t]|)          required gain
//   h[t]  = min(r[t-L .. t])                             sliding minimum, L+1 values
//   s[t]  = mean(h[t-L+1 .. t])                          L-sample box smoothing
//   g[t]  = s[t] if falling, else one-pole release towards s[t]
//   y[t]  = x[t-L] * g[t]
// Every h[k] in the averaging window covers sample t-L, so s[t] <= r[t-L] and
// therefore |y[t]| <= ceiling. The gain reaches its target with a linear ramp
// over L samples, so there is no instantaneous gain step (no hard clipping).
//
// Latency: exactly L samples, which the host must be told about.

#include <vector>

namespace mixcore::dsp {

class LookaheadPeakGuard
{
public:
    /// Allocates. `lookaheadSamples` >= 1.
    void prepare(int numChannels, int lookaheadSamples, double sampleRate);
    void reset() noexcept;

    void setCeilingDb(float ceilingDb) noexcept;
    void setReleaseMs(float releaseMs) noexcept;

    /// Process one interleaved frame of `numChannels` samples in place.
    /// Returns the gain applied to the (delayed) output frame, linear.
    float processFrame(float* frame, int numChannels) noexcept;

    int latencySamples() const noexcept { return lookahead_; }

private:
    int numChannels_ = 0;
    int lookahead_ = 1;
    double sampleRate_ = 48000.0;
    float ceiling_ = 0.70794578f; // -3 dBFS
    float releaseCoef_ = 0.0f;
    float envelope_ = 1.0f;

    // Audio delay: ring of (lookahead_ + 1) frames.
    std::vector<float> delay_;
    int delayPos_ = 0;
    int delayLength_ = 2;

    // Monotonic deque for the sliding minimum (circular storage).
    std::vector<float> dqValue_;
    std::vector<long long> dqTime_;
    int dqHead_ = 0;
    int dqCount_ = 0;
    int dqCapacity_ = 2;
    long long time_ = 0;

    // Box smoother.
    std::vector<double> box_;
    double boxSum_ = 0.0;
    int boxPos_ = 0;
};

} // namespace mixcore::dsp
