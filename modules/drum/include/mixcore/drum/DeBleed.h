#pragma once
// De-bleed: hit-correlated high-band bleed suppression for drum close mics.
//
// The problem it solves: a gate only removes bleed BETWEEN hits. A crash or
// hi-hat that rings over a snare hit passes straight through the open gate.
//
// Principle: a drum's own high end (snare wires, stick attack) rises and
// falls with its body. MixCore learns how much high end THIS drum makes per
// unit of body, and treats high end beyond that as bleed.
//
//   low  = LR4 low-pass(x)  at crossover   (drum body; never changed)
//   high = LR4 high-pass(x) at crossover   (wires/attack + cymbal bleed)
//
//   bodyEnv  = peak envelope of |low|     (instant attack, 20 ms release)
//   highEnv  = peak envelope of |high|    (instant attack, 20 ms release)
//   allowed  = max(k * bodyEnv, allowed * decay(length))   k = learned high/body ratio
//   D        = allowed * tolerance                         (high end the drum explains)
//   B        = bleed estimate (high band, followed only while D < highEnv / 2)
//   g        = clamp(min(D / highEnv, D / sqrt(D^2 + B^2)), maxReduction, 1)
//   y        = low + g * high          (g smoothed: opens in 0.3 ms, closes in 2 ms)
//
// k is learned from the first ~6 ms of each detected hit (a hit = body onset
// within 12 dB of the loudest recent hit). It starts permissive (+10 dB, i.e.
// almost no processing), moves DOWN quickly (40 % per hit) and UP slowly
// (10 % per hit), and ignores hits more than 6 dB brighter than learned. So
// the drum's own brightness is found within a few hits, while hits that a
// cymbal lands on don't teach it that the drum is bright.
//
// No lookahead, no FFT: zero added latency. When disabled the input passes
// through bit-exact; enabling/disabling crossfades over 10 ms.

#include "mixcore/core/TripleBuffer.h"
#include "mixcore/dsp/Biquad.h"
#include "mixcore/drum/DrumSettings.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace mixcore::drum {

struct DeBleedTelemetry
{
    std::uint64_t sequence = 0;
    bool enabled = false;
    float reductionDb = 0.0f;      ///< Max high-band reduction in the last 10 ms (>= 0).
    float bodyDb = -150.0f;        ///< Body envelope (dBFS), last tick max.
    float highDb = -150.0f;        ///< High-band envelope (dBFS), last tick max.
    float learnedRatioDb = 10.0f;  ///< Learned high/body ratio of the drum's own hits.
    std::uint32_t hitsSeen = 0;
};

class DeBleed
{
public:
    static constexpr int kMaxChannels = 8;

    bool prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setSettings(const DrumSettings& s) noexcept;
    void setBypassed(bool b) noexcept { bypassed_ = b; }

    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    /// Forget the learned drum brightness (e.g. after changing the drum or mic).
    void resetLearning() noexcept { resetLearning_.store(true, std::memory_order_release); }

    bool readTelemetry(DeBleedTelemetry& out) noexcept { return telemetry_.read(out); }
    int latencySamples() const noexcept { return 0; }

private:
    void design() noexcept;

    DrumSettings settings_;
    bool prepared_ = false;
    bool bypassed_ = false;
    std::atomic<bool> resetLearning_ { false };
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    float designedCrossover_ = -1.0f;

    // LR4 = two cascaded Butterworth (Q 0.7071) biquads per band, per channel.
    std::array<std::array<dsp::Biquad, 2>, kMaxChannels> lp_ {};
    std::array<std::array<dsp::Biquad, 2>, kMaxChannels> hp_ {};

    float envRelease_ = 0.0f;
    float bodyEnv_ = 0.0f;
    float highEnv_ = 0.0f;
    float allowed_ = 0.0f;
    float lengthCoef_ = 0.0f;
    float slowBody_ = 0.0f;
    float slowCoef_ = 0.0f;
    float hitLevel_ = 0.0f;     // slowly decaying max of the body envelope (loudest recent hit)
    float hitLevelCoef_ = 0.0f;
    float bleed_ = 0.0f;        // estimate of the steady bleed in the high band
    float bleedCoef_ = 0.0f;

    // Hit learning
    float logK_ = 0.0f;        // log10 of learned high/body amplitude ratio
    float kLin_ = 1.0f;        // 10^logK_
    int refractory_ = 0;
    int windowLeft_ = 0;
    int windowSamples_ = 1;
    int refractorySamples_ = 1;
    float winBodyMax_ = 0.0f;
    float winHighMax_ = 0.0f;
    std::uint32_t hits_ = 0;

    // Gain
    float gainDb_ = 0.0f;
    float openCoef_ = 0.0f;
    float closeCoef_ = 0.0f;
    float mix_ = 0.0f;
    float mixStep_ = 0.0f;

    // Telemetry
    int hop_ = 480;
    int tick_ = 0;
    float tickMaxRed_ = 0.0f;
    float tickBody_ = 0.0f;
    float tickHigh_ = 0.0f;
    DeBleedTelemetry snap_;
    TripleBuffer<DeBleedTelemetry> telemetry_;
};

} // namespace mixcore::drum
