#pragma once
// MixCore saturation.
//
//   x -> drive -> curve (Tape / Tube / Console / Clip) -> DC blocker
//     -> auto gain (wet loudness matched to the dry signal)
//     -> parallel mix with the dry signal -> output gain -> 10 ms on/off fade
//
// Live quality runs at the host rate with zero latency. Studio runs the curve
// at 4x (two linear-phase halfband stages, 28 samples latency, reported only
// while the module is on in Studio). In Studio the dry part of the mix passes
// through the same filters, so parallel mixing never comb-filters. Off = bit-
// exact pass-through. Realtime-safe: no allocation, no locks.

#include "mixcore/comp/LevelTap.h"
#include "mixcore/core/TripleBuffer.h"
#include "mixcore/sat/Oversampler.h"
#include "mixcore/sat/SatSettings.h"

#include <array>
#include <cstdint>

namespace mixcore::sat {

struct SatTelemetry
{
    std::uint64_t sequence = 0;
    bool enabled = false;
    float inputPeakDb = -150.0f;     ///< before drive, last 10 ms
    float saturationDb = -150.0f;    ///< energy the curve adds or removes, relative to the driven signal
    float autoGainDb = 0.0f;         ///< auto gain applied to the saturated signal
    float outputPeakDb = -150.0f;
    double sampleRate = 48000.0;
};

class Saturator
{
public:
    static constexpr int kMaxChannels = 8;

    bool prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setSettings(const SatSettings& s) noexcept;
    void setBypassed(bool b) noexcept { bypassed_ = b; }
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    /// 28 samples while on in Studio quality, otherwise 0.
    int latencySamples() const noexcept;
    bool readTelemetry(SatTelemetry& out) noexcept { return telemetry_.read(out); }
    const comp::LevelTap& levelTap() const noexcept { return tap_; }

private:
    float wetSample(int ch, float x) noexcept;

    SatSettings settings_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    bool prepared_ = false, bypassed_ = false;
    bool studio_ = false; // the quality the audio path is running in

    std::array<Oversampler4x, kMaxChannels> os_ {};
    static constexpr int kDelay = Oversampler4x::latencySamples();
    std::array<std::array<float, kDelay>, kMaxChannels> dryDelay_ {};
    int delayPos_ = 0;
    std::array<float, kMaxChannels> inputs_ {};
    std::array<double, kMaxChannels> dcX_ {}, dcY_ {};
    double dcR_ = 0.999, dcR4_ = 0.999;

    // Smoothed controls
    double drive_ = 1.0, mix_ = 1.0, out_ = 1.0, agc_ = 1.0, smooth_ = 0.0;
    float moduleMix_ = 0.0f, mixStep_ = 0.0f;
    // Auto gain power trackers
    double pDry_ = 0.0, pWet_ = 0.0, powCoef_ = 0.0, agcCoef_ = 0.0;
    std::int64_t learned_ = 0; // samples of signal seen since switching on
    // Per-sample accumulators for auto gain and telemetry (summed over channels / oversampled points)
    double accDry_ = 0.0, accWet_ = 0.0, accLin_ = 0.0, accDist_ = 0.0;

    int hop_ = 480, hopCount_ = 0;
    double hopInSq_ = 0.0, hopLin_ = 0.0, hopDist_ = 0.0;
    float hopInPeak_ = 0.0f, hopOutPeak_ = 0.0f;
    comp::LevelTap tap_;
    SatTelemetry snap_;
    TripleBuffer<SatTelemetry> telemetry_;
};

} // namespace mixcore::sat
