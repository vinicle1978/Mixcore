#pragma once
// One MixCore channel: Smart Gain Stage -> Drum de-bleed -> Gate, in place.
//
// Order follows the product workflow (stage 1 gain staging, stage 2 gating):
// the gate's threshold is relative to the calibrated level, so calibrate
// first, then learn the gate threshold. De-bleed sits before the gate so it
// can measure the raw bleed level between hits (its in-hit reduction depends
// on that estimate) and so the gate sees less cymbal wash.
//
// Latency  = Smart Gain latency (peak guard) + gate latency (lookahead);
//            de-bleed adds none.
// Bypass   = whole strip: both modules pass audio through their own
//            latency-matched delays, so a bypassed strip is the input delayed
//            by exactly the reported latency.

#include "mixcore/gain/SmartGainStage.h"
#include "mixcore/drum/DeBleed.h"
#include "mixcore/gate/NoiseGate.h"

#include <string>

namespace mixcore::strip {

struct ChannelSettings
{
    gain::SmartGainSettings gain;
    gate::GateSettings gate;
    drum::DrumSettings drum;

    void validate() noexcept
    {
        gain.validate();
        gate.validate();
        drum.validate();
    }
    bool operator==(const ChannelSettings&) const = default;
};

class ChannelStrip
{
public:
    bool prepare(double sampleRate, int maxBlockSize, int numChannels)
    {
        return gain_.prepare(sampleRate, maxBlockSize, numChannels) && drum_.prepare(sampleRate, maxBlockSize, numChannels)
               && gate_.prepare(sampleRate, maxBlockSize, numChannels);
    }

    void reset() noexcept
    {
        gain_.reset();
        drum_.reset();
        gate_.reset();
    }

    /// Realtime-safe.
    void setSettings(const ChannelSettings& s) noexcept
    {
        gain_.setSettings(s.gain);
        drum_.setBypassed(s.gain.bypass);
        drum_.setSettings(s.drum);
        gate_.setBypassed(s.gain.bypass);
        gate_.setSettings(s.gate);
    }

    /// Realtime-safe.
    void process(float* const* channels, int numChannels, int numSamples) noexcept
    {
        gain_.process(channels, numChannels, numSamples);
        drum_.process(channels, numChannels, numSamples);
        gate_.process(channels, numChannels, numSamples);
    }

    int latencySamples() const noexcept { return gain_.latencySamples() + gate_.latencySamples(); }

    gain::SmartGainStage& gain() noexcept { return gain_; }
    gate::NoiseGate& gate() noexcept { return gate_; }
    drum::DeBleed& debleed() noexcept { return drum_; }
    const gain::SmartGainStage& gain() const noexcept { return gain_; }
    const gate::NoiseGate& gate() const noexcept { return gate_; }
    const drum::DeBleed& debleed() const noexcept { return drum_; }

private:
    gain::SmartGainStage gain_;
    drum::DeBleed drum_;
    gate::NoiseGate gate_;
};

} // namespace mixcore::strip
