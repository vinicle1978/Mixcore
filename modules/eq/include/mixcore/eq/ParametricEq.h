#pragma once
// MixCore parametric EQ: 7 bands, zero latency, smoothed parameters.
//
// Parameter changes glide (20 ms, coefficients recomputed every 32 samples) so
// moving or automating a band never clicks. Switching a band on/off crossfades
// its output over 10 ms; switching the whole EQ on/off crossfades over 10 ms
// and, once off, passes audio through bit-exact. Two analyzer taps (before and
// after the EQ, mono) feed the UI.

#include "mixcore/core/TripleBuffer.h"
#include "mixcore/eq/EqFilters.h"
#include "mixcore/eq/EqSettings.h"
#include "mixcore/eq/SpectrumTap.h"

#include <array>
#include <cstdint>

namespace mixcore::eq {

struct EqTelemetry
{
    std::uint64_t sequence = 0;
    bool enabled = false;
    int activeBands = 0;
    double sampleRate = 48000.0;
};

class ParametricEq
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr int kMaxSections = 4;

    bool prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setSettings(const EqSettings& s) noexcept;
    void setBypassed(bool b) noexcept { bypassed_ = b; }
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    int latencySamples() const noexcept { return 0; }
    double sampleRate() const noexcept { return sampleRate_; }
    bool readTelemetry(EqTelemetry& out) noexcept { return telemetry_.read(out); }

    /// Analyzer taps (mono): signal entering and leaving the EQ.
    const SpectrumTap& inputTap() const noexcept { return inTap_; }
    const SpectrumTap& outputTap() const noexcept { return outTap_; }

private:
    struct State
    {
        double z1 = 0.0, z2 = 0.0;
    };
    struct BandRuntime
    {
        // Smoothed parameters (log frequency, dB gain, log Q).
        double logF = 0.0, gainDb = 0.0, logQ = 0.0;
        float mix = 0.0f; // band on/off crossfade
        BandDesign design;
        std::array<std::array<State, kMaxSections>, kMaxChannels> state {};
        bool designed = false;
    };

    void updateCoefficients(bool snap) noexcept;

    EqSettings settings_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    bool prepared_ = false;
    bool bypassed_ = false;

    std::array<BandRuntime, kNumBands> bands_ {};
    double smoothCoef_ = 0.0;   // per control tick (32 samples)
    float mixStep_ = 0.0f;      // per sample, 10 ms
    float moduleMix_ = 0.0f;
    float outGain_ = 1.0f, outGainCoef_ = 0.0f;
    int controlCounter_ = 0;

    SpectrumTap inTap_, outTap_;
    int tick_ = 0, hop_ = 480;
    EqTelemetry snap_;
    TripleBuffer<EqTelemetry> telemetry_;
};

} // namespace mixcore::eq
