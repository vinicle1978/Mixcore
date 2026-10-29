#pragma once
// MixCore gate / downward expander. Realtime-safe, stereo-linked.
//
// Per sample:
//   sc_ch  = LPF(HPF(x_ch))     side-chain, per channel; 24 dB/oct Butterworth
//                               (two cascaded biquads each) so a kick key at
//                               150 Hz really ignores a 200 Hz snare
//   det    = max_ch |sc_ch|  -> peak envelope (instant attack, 10 ms release)
//            or one-pole 10 ms RMS of max_ch sc_ch^2           (rmsDetector)
//   state  : Closed --(det >= thr [and transient rise])--> Open
//            Open   --(det <  thr - hyst)---------------> Hold
//            Hold   --(det >= thr - hyst)---------------> Open
//            Hold   --(holdMs elapsed)-----------------> Closed
//   target : Open/Hold -> 0 dB
//            Closed    -> hard gate: -range
//                         expander:  -min(range, (thr - det) * (ratio - 1))
//   gainDb : moves towards target in a straight line in dB:
//            opening at range/attackMs, closing at range/releaseMs
//   y      = x delayed by lookahead * dbToGain(gainDb)
//
// The detector sees the signal `lookahead` earlier than the audio it
// controls, so the gate is already open when a drum hit arrives.
// Latency = lookahead (reported). When disabled, audio still passes through
// the same delay so total latency does not jump; enable/disable crossfades
// over 10 ms.

#include "mixcore/core/TripleBuffer.h"
#include "mixcore/dsp/Biquad.h"
#include "mixcore/dsp/BlockMaxWindow.h"
#include "mixcore/dsp/RollingPower.h"
#include "mixcore/gate/GateLearner.h"
#include "mixcore/gate/GateSettings.h"
#include "mixcore/gate/GateTelemetry.h"

#include <array>
#include <atomic>
#include <vector>

namespace mixcore::gate {

class NoiseGate
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr double kMaxLookaheadMs = 5.0;
    static constexpr double kControlIntervalMs = 10.0;

    bool prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;

    void setSettings(const GateSettings& settings) noexcept;
    const GateSettings& settings() const noexcept { return settings_; }

    /// When true the gate passes audio unchanged (but still delayed by its
    /// latency). Used for whole-strip bypass. Crossfaded.
    void setBypassed(bool bypassed) noexcept { bypassed_ = bypassed; }

    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    int latencySamples() const noexcept { return latency_.load(std::memory_order_relaxed); }
    bool readTelemetry(GateTelemetry& out) noexcept { return telemetry_.read(out); }
    GateLearner& learner() noexcept { return learner_; }
    const GateLearner& learner() const noexcept { return learner_; }
    double sampleRate() const noexcept { return sampleRate_; }

private:
    void updateCoefficients() noexcept;
    void controlTick() noexcept;

    GateSettings settings_;
    bool prepared_ = false;
    bool bypassed_ = false;
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    int hopSamples_ = 480;
    int tickCounter_ = 0;

    // Side-chain
    // 4th-order Butterworth = two biquads with Q 0.5412 and 1.3066.
    std::array<std::array<dsp::Biquad, 2>, kMaxChannels> hpf_ {};
    std::array<std::array<dsp::Biquad, 2>, kMaxChannels> lpf_ {};
    bool useHpf_ = false;
    bool useLpf_ = false;
    float designedHpf_ = 0.0f, designedLpf_ = 0.0f;
    float peakEnv_ = 0.0f;
    float peakReleaseCoef_ = 0.0f;
    double rmsPower_ = 0.0;
    float rmsCoef_ = 0.0f;
    float slowEnv_ = 0.0f; // slow detector envelope (linear), for transient trigger
    float slowCoef_ = 0.0f;

    // State machine
    GateState state_ = GateState::Closed;
    int holdSamples_ = 0;
    int holdCounter_ = 0;
    double gainDb_ = 0.0;
    double openStepDb_ = 1.0;
    double closeStepDb_ = 1.0;
    float gain_ = 1.0f;

    // Lookahead delay (shared length for gated and dry paths)
    std::vector<float> delay_;
    int delayCapacity_ = 1;
    int delayPos_ = 0;
    int lookahead_ = 0;
    int wantLookahead_ = 0;
    float fade_ = 1.0f;
    float fadeStep_ = 0.0f;
    float mix_ = 0.0f;  // 0 = dry (disabled/bypassed), 1 = gated
    float mixStep_ = 0.0f;
    std::atomic<int> latency_ { 0 };

    // Telemetry accumulators
    dsp::RollingPower outPower_;
    dsp::BlockMaxWindow outPeakPublish_;
    float tickOutPeak_ = 0.0f;
    float tickDetMax_ = 0.0f;
    float tickMinGainDb_ = 0.0f;
    std::uint32_t opensTotal_ = 0;
    std::array<std::uint8_t, 100> opensPerTick_ {};
    int opensPos_ = 0;
    std::uint8_t opensThisTick_ = 0;
    int closedTicks_ = 0;

    GateLearner learner_;
    GateTelemetry snapshot_;
    TripleBuffer<GateTelemetry> telemetry_;
};

} // namespace mixcore::gate
