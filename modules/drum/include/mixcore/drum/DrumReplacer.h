#pragma once
// Drum replacement / augmentation: detects hits on the drum mic and plays
// samples from a kit, mixed with (or instead of) the original.
//
//   key (pre-gate) -> detector band filters (HPF/LPF, 24 dB/oct) -> peak envelope
//   hit  = envelope reaches the threshold while re-armed (stayed 4 dB below it
//          for 15 ms, or rose 6 dB suddenly) and Retrigger time has passed
//   velocity = loudest key peak during the Lookahead window after the hit,
//          mapped over Range dB above the threshold, scaled by Dynamics
//   sample = velocity layer (quiet -> loud) + round robin, pitch, attack, length
//   output = original (delayed by Lookahead) x Original level + samples x Sample level
//
// Timing: the original is delayed by the lookahead; each sample starts exactly
// where the hit's threshold crossing lands in the delayed original, so
// original and sample stay phase-aligned. Latency = lookahead while active
// (0 when replacement is off). If the audio input is already delayed relative
// to the key (the gate's lookahead), setKeyOffset() compensates.
//
// Realtime: fixed voice pool, fixed trigger queue, delay lines sized in
// prepare(); kits arrive through KitExchange. Off and settled = bit-exact
// pass-through with zero latency.

#include "mixcore/core/TripleBuffer.h"
#include "mixcore/drum/DrumSettings.h"
#include "mixcore/drum/SampleKit.h"
#include "mixcore/dsp/Biquad.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace mixcore::drum {

struct ReplaceTelemetry
{
    std::uint64_t sequence = 0;
    bool enabled = false;
    bool kitLoaded = false;
    float detectorDb = -150.0f;  ///< Detector peak in the last 10 ms.
    float thresholdDb = -24.0f;
    int hitsThisTick = 0;        ///< Hits detected in the last 10 ms.
    float lastVelocity = 0.0f;   ///< 0..1, most recent hit.
    std::uint32_t totalHits = 0;
    int hitsLastSecond = 0;
    float secondsSinceHit = 1.0e6f;
    int activeVoices = 0;
    int latencySamples = 0;
};

class DrumReplacer
{
public:
    static constexpr int kMaxChannels = 8;
    static constexpr int kMaxVoices = 16;
    static constexpr int kMaxLayers = 16;
    static constexpr double kMaxLookaheadMs = 10.0;
    static constexpr double kMaxKeyOffsetMs = 10.0;

    bool prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setSettings(const DrumSettings& s) noexcept;
    void setBypassed(bool b) noexcept { bypassed_ = b; }
    /// Samples by which `audio` lags `key` (e.g. the gate's lookahead).
    void setKeyOffset(int samples) noexcept;

    /// `audio`: processed in place. `key`: what the detector listens to (may
    /// be the same buffers as `audio`).
    void process(float* const* audio, const float* const* key, int numChannels, int numSamples) noexcept;

    KitExchange& kits() noexcept { return kits_; }
    int latencySamples() const noexcept { return latency_.load(std::memory_order_relaxed); }
    bool readTelemetry(ReplaceTelemetry& out) noexcept { return telemetry_.read(out); }

private:
    struct Voice
    {
        const SampleBuffer* sample = nullptr;
        double pos = 0.0;
        double rate = 1.0;
        float gain = 0.0f;
        int age = 0;
        float fadeOut = 1.0f;     ///< < 1 while being choked/stolen
        float fadeOutStep = 0.0f;
        std::uint32_t order = 0;  ///< Start order (oldest = smallest).
    };
    struct PendingHit
    {
        int countdown = 0;
        float velocity = 0.0f;
        bool used = false;
    };

    void design() noexcept;
    void startVoice(float velocity) noexcept;
    void renderVoices(float* outL, float* outR, int nOutCh) noexcept;

    DrumSettings settings_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    bool prepared_ = false;
    bool bypassed_ = false;

    // Detector
    std::array<std::array<dsp::Biquad, 2>, kMaxChannels> hpf_ {};
    std::array<std::array<dsp::Biquad, 2>, kMaxChannels> lpf_ {};
    bool useHpf_ = false, useLpf_ = false;
    float designedLow_ = -1.0f, designedHigh_ = -1.0f;
    float env_ = 0.0f, envRelease_ = 0.0f, slowEnv_ = 0.0f, slowCoef_ = 0.0f;
    bool armed_ = true;
    int belowCount_ = 0, rearmHold_ = 720;
    int sinceLast_ = 1 << 30;
    int windowLeft_ = 0;
    float windowPeak_ = 0.0f;

    // Timing
    int wantLatency_ = 0;
    std::atomic<int> latency_ { 0 };
    int keyOffset_ = 0;
    std::vector<float> delay_; // interleaved [pos * kMaxChannels + ch]
    int delayCapacity_ = 1;
    int delayPos_ = 0;
    float fade_ = 1.0f, fadeStep_ = 0.0f;
    float mix_ = 0.0f, mixStep_ = 0.0f;
    float originalGain_ = 1.0f, sampleGain_ = 1.0f, gainCoef_ = 0.0f;
    std::array<PendingHit, 32> pending_ {};

    // Sampler
    KitExchange kits_;
    const SampleKit* kit_ = nullptr;
    std::array<Voice, kMaxVoices> voices_ {};
    std::array<std::uint32_t, kMaxLayers> roundRobin_ {};
    std::uint32_t voiceOrder_ = 0;
    int attackSamples_ = 0, lengthSamples_ = 0, lengthFadeSamples_ = 1;
    double pitchRatio_ = 1.0;

    // Telemetry
    int hop_ = 480, tick_ = 0;
    float tickDet_ = 0.0f;
    int tickHits_ = 0;
    float lastVelocity_ = 0.0f;
    std::uint32_t totalHits_ = 0;
    std::array<std::uint8_t, 100> hitRing_ {}; // hits per 10 ms tick, last second
    int hitRingPos_ = 0, hitsLastSecond_ = 0;
    float secondsSinceHit_ = 1.0e6f;
    ReplaceTelemetry snap_;
    TripleBuffer<ReplaceTelemetry> telemetry_;
};

} // namespace mixcore::drum
