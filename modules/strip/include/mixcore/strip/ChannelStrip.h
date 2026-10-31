#pragma once
// One MixCore channel, in place:
//   Smart Gain -> Drum de-bleed -> Gate -> Drum replacement -> EQ
// (replacement listens to the signal *before* the gate, so the gate never
// stops it detecting quiet hits, and its samples are added after the gate, so
// the gate never cuts their tails).
//
// Order follows the product workflow (stage 1 gain staging, stage 2 gating):
// the gate's threshold is relative to the calibrated level, so calibrate
// first, then learn the gate threshold. De-bleed sits before the gate so it
// can measure the raw bleed level between hits (its in-hit reduction depends
// on that estimate) and so the gate sees less cymbal wash.
//
// Latency  = Smart Gain latency (peak guard) + gate latency (lookahead)
//            + replacement lookahead (only while replacement is on);
//            de-bleed and EQ add none.
// Bypass   = whole strip: both modules pass audio through their own
//            latency-matched delays, so a bypassed strip is the input delayed
//            by exactly the reported latency.

#include "mixcore/gain/SmartGainStage.h"
#include "mixcore/gain/SourceType.h"
#include "mixcore/drum/DeBleed.h"
#include "mixcore/drum/DrumReplacer.h"
#include "mixcore/eq/ParametricEq.h"
#include "mixcore/gate/NoiseGate.h"

#include <algorithm>
#include <array>
#include <vector>

#include <string>

namespace mixcore::strip {

struct ChannelSettings
{
    /// Module power: Smart Gain is on by default; gate and de-bleed carry their
    /// own `enabled` flags (off by default). A module that is off passes audio
    /// through untouched (crossfaded over 10 ms).
    bool gainOn = true;
    /// What this channel carries; changes wording (Mix Coach, status), not DSP.
    gain::SourceType source = gain::SourceType::Vocal;
    gain::SmartGainSettings gain;
    gate::GateSettings gate;
    drum::DrumSettings drum;
    eq::EqSettings eq;

    void validate() noexcept
    {
        gain.validate();
        gate.validate();
        drum.validate();
        eq.validate();
    }
    bool operator==(const ChannelSettings&) const = default;
};

class ChannelStrip
{
public:
    bool prepare(double sampleRate, int maxBlockSize, int numChannels)
    {
        chunk_ = std::max(64, std::min(maxBlockSize, 4096));
        channels_ = std::clamp(numChannels, 1, drum::DrumReplacer::kMaxChannels);
        key_.assign(static_cast<size_t>(chunk_) * static_cast<size_t>(channels_), 0.0f);
        return gain_.prepare(sampleRate, maxBlockSize, numChannels) && drum_.prepare(sampleRate, maxBlockSize, numChannels)
               && gate_.prepare(sampleRate, maxBlockSize, numChannels)
               && replacer_.prepare(sampleRate, maxBlockSize, numChannels) && eq_.prepare(sampleRate, maxBlockSize, numChannels);
    }

    void reset() noexcept
    {
        gain_.reset();
        drum_.reset();
        gate_.reset();
        replacer_.reset();
        eq_.reset();
    }

    /// Realtime-safe.
    void setSettings(const ChannelSettings& s) noexcept
    {
        auto g = s.gain;
        g.bypass = s.gain.bypass || !s.gainOn; // module off = Smart Gain's own latency-matched bypass
        gain_.setSettings(g);
        drum_.setBypassed(s.gain.bypass);
        drum_.setSettings(s.drum);
        gate_.setBypassed(s.gain.bypass);
        gate_.setSettings(s.gate);
        replacer_.setBypassed(s.gain.bypass);
        replacer_.setSettings(s.drum);
        eq_.setBypassed(s.gain.bypass);
        eq_.setSettings(s.eq);
    }

    /// Realtime-safe.
    void process(float* const* channels, int numChannels, int numSamples) noexcept
    {
        gain_.process(channels, numChannels, numSamples);
        drum_.process(channels, numChannels, numSamples);
        // Keep a pre-gate copy for the replacement detector (in chunks, so
        // blocks larger than the prepared size still work without allocating).
        const int nCh = std::min(numChannels, channels_);
        for (int start = 0; start < numSamples; start += chunk_)
        {
            const int n = std::min(chunk_, numSamples - start);
            std::array<float*, drum::DrumReplacer::kMaxChannels> io {};
            std::array<const float*, drum::DrumReplacer::kMaxChannels> key {};
            for (int ch = 0; ch < nCh; ++ch)
            {
                float* k = key_.data() + static_cast<size_t>(ch) * static_cast<size_t>(chunk_);
                std::copy(channels[ch] + start, channels[ch] + start + n, k);
                io[static_cast<size_t>(ch)] = channels[ch] + start;
                key[static_cast<size_t>(ch)] = k;
            }
            gate_.process(io.data(), nCh, n);
            replacer_.setKeyOffset(gate_.latencySamples());
            replacer_.process(io.data(), key.data(), nCh, n);
            eq_.process(io.data(), nCh, n);
        }
    }

    int latencySamples() const noexcept
    {
        return gain_.latencySamples() + gate_.latencySamples() + replacer_.latencySamples();
    }

    gain::SmartGainStage& gain() noexcept { return gain_; }
    gate::NoiseGate& gate() noexcept { return gate_; }
    drum::DeBleed& debleed() noexcept { return drum_; }
    drum::DrumReplacer& replacer() noexcept { return replacer_; }
    eq::ParametricEq& eq() noexcept { return eq_; }
    const eq::ParametricEq& eq() const noexcept { return eq_; }
    const gain::SmartGainStage& gain() const noexcept { return gain_; }
    const gate::NoiseGate& gate() const noexcept { return gate_; }
    const drum::DeBleed& debleed() const noexcept { return drum_; }

private:
    gain::SmartGainStage gain_;
    drum::DeBleed drum_;
    gate::NoiseGate gate_;
    drum::DrumReplacer replacer_;
    eq::ParametricEq eq_;
    std::vector<float> key_;
    int chunk_ = 512;
    int channels_ = 1;
};

} // namespace mixcore::strip
