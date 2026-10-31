#pragma once
// MixCore compressor: feed-forward, zero latency, stereo-linked.
//
//   sidechain (optional 12 dB/oct high-pass) -> detector (peak or 10 ms RMS,
//   loudest channel) -> soft-knee gain computer -> attack/release smoothing
//   in dB (optional program-dependent release) -> makeup (manual or auto) ->
//   parallel mix -> 10 ms on/off crossfade.
//
// The dry/wet mix and the module crossfade collapse into one gain per sample
// (no delay is needed: the wet path has no latency), so parallel compression
// never comb-filters. Off = bit-exact pass-through. No allocation, no locks.

#include "mixcore/comp/CompSettings.h"
#include "mixcore/comp/LevelTap.h"
#include "mixcore/core/TripleBuffer.h"
#include "mixcore/dsp/Biquad.h"

#include <array>
#include <cstdint>

namespace mixcore::comp {

struct CompTelemetry
{
    std::uint64_t sequence = 0;
    bool enabled = false;
    float gainReductionDb = 0.0f; ///< >= 0, largest reduction in the last 10 ms
    float detectorDb = -150.0f;   ///< largest detector level in the last 10 ms
    float makeupDb = 0.0f;        ///< makeup being applied (auto or manual)
    float outputRmsDb = -150.0f;  ///< 10 ms RMS after the compressor
    double sampleRate = 48000.0;
};

class Compressor
{
public:
    static constexpr int kMaxChannels = 8;

    bool prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setSettings(const CompSettings& s) noexcept;
    void setBypassed(bool b) noexcept { bypassed_ = b; }
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    int latencySamples() const noexcept { return 0; }
    double sampleRate() const noexcept { return sampleRate_; }
    bool readTelemetry(CompTelemetry& out) noexcept { return telemetry_.read(out); }
    /// Short-term levels entering the compressor (recorded even while it is off).
    const LevelTap& levelTap() const noexcept { return tap_; }

private:
    void updateCoefficients() noexcept;

    CompSettings settings_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    bool prepared_ = false, bypassed_ = false;

    std::array<dsp::Biquad, kMaxChannels> scHpf_ {};
    float hpfDesignedHz_ = -1.0f;
    double rmsCoef_ = 0.0, ms_ = 0.0;
    double attCoef_ = 0.0, relCoef_ = 0.0, slowAttCoef_ = 0.0, slowRelCoef_ = 0.0;
    double gr_ = 0.0, grSlow_ = 0.0;          // dB, <= 0
    double avgGr_ = 0.0, avgCoef_ = 0.0;      // auto makeup tracker
    double makeup_ = 0.0, smoothCoef_ = 0.0;  // dB applied
    double mix_ = 1.0;
    float moduleMix_ = 0.0f, mixStep_ = 0.0f;

    // 10 ms reporting hop
    int hop_ = 480, hopCount_ = 0;
    double hopInSq_ = 0.0, hopOutSq_ = 0.0;
    float hopInPeak_ = 0.0f, hopMaxGr_ = 0.0f, hopMaxLevel_ = -150.0f;
    LevelTap tap_;
    CompTelemetry snap_;
    TripleBuffer<CompTelemetry> telemetry_;
};

} // namespace mixcore::comp
