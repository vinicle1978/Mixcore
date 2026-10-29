#pragma once
// One MixCore channel: Smart Gain Stage -> Gate, processed in place.
//
// Order follows the product workflow (stage 1 gain staging, stage 2 gating):
// the gate's threshold is relative to the calibrated level, so calibrate
// first, then learn the gate threshold.
//
// Latency  = Smart Gain latency (peak guard) + gate latency (lookahead).
// Bypass   = whole strip: both modules pass audio through their own
//            latency-matched delays, so a bypassed strip is the input delayed
//            by exactly the reported latency.

#include "mixcore/gain/SmartGainStage.h"
#include "mixcore/gate/NoiseGate.h"

#include <string>

namespace mixcore::strip {

struct ChannelSettings
{
    gain::SmartGainSettings gain;
    gate::GateSettings gate;

    void validate() noexcept
    {
        gain.validate();
        gate.validate();
    }
    bool operator==(const ChannelSettings&) const = default;
};

class ChannelStrip
{
public:
    bool prepare(double sampleRate, int maxBlockSize, int numChannels)
    {
        return gain_.prepare(sampleRate, maxBlockSize, numChannels) && gate_.prepare(sampleRate, maxBlockSize, numChannels);
    }

    void reset() noexcept
    {
        gain_.reset();
        gate_.reset();
    }

    /// Realtime-safe.
    void setSettings(const ChannelSettings& s) noexcept
    {
        gain_.setSettings(s.gain);
        gate_.setBypassed(s.gain.bypass);
        gate_.setSettings(s.gate);
    }

    /// Realtime-safe.
    void process(float* const* channels, int numChannels, int numSamples) noexcept
    {
        gain_.process(channels, numChannels, numSamples);
        gate_.process(channels, numChannels, numSamples);
    }

    int latencySamples() const noexcept { return gain_.latencySamples() + gate_.latencySamples(); }

    gain::SmartGainStage& gain() noexcept { return gain_; }
    gate::NoiseGate& gate() noexcept { return gate_; }
    const gain::SmartGainStage& gain() const noexcept { return gain_; }
    const gate::NoiseGate& gate() const noexcept { return gate_; }

private:
    gain::SmartGainStage gain_;
    gate::NoiseGate gate_;
};

} // namespace mixcore::strip
