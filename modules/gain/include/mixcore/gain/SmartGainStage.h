#pragma once
// Smart Gain Stage: static gain + adaptive vocal rider + peak safety +
// telemetry, as one realtime-safe processor.
//
// Signal flow (per sample, stereo-linked):
//
//   in -> [sanitise] -> [input meters, clip detect, detectors] ->
//         x staticGain x riderGain -> [optional lookahead peak guard] ->
//         [bypass crossfade with latency-aligned dry] -> [output meters] -> out
//
// All measurement and control decisions run per sample / per control tick
// with sample-count-based timing, so output is identical for any host block
// size (verified by tests).
//
// Thread model:
//   prepare()/reset()            : non-realtime (allocates)
//   setSettings()/process()      : audio thread, no allocation, no locks
//   readTelemetry()              : one consumer thread (UI), wait-free
//   calibrator() control calls   : control/UI thread (see StaticCalibrator.h)

#include "mixcore/core/TripleBuffer.h"
#include "mixcore/dsp/Biquad.h"
#include "mixcore/dsp/BlockMaxWindow.h"
#include "mixcore/dsp/LookaheadPeakGuard.h"
#include "mixcore/dsp/RollingPower.h"
#include "mixcore/dsp/TruePeakEstimator.h"
#include "mixcore/gain/ActivityDetector.h"
#include "mixcore/gain/NoiseFloorTracker.h"
#include "mixcore/gain/SmartGainSettings.h"
#include "mixcore/gain/SmartGainTelemetry.h"
#include "mixcore/gain/StaticCalibrator.h"
#include "mixcore/gain/VocalRider.h"

#include <array>
#include <atomic>
#include <vector>

namespace mixcore::gain {

class SmartGainStage
{
public:
    static constexpr int kMaxChannels = 8;

    // Fixed analysis timing (from the product spec).
    static constexpr double kPrimaryWindowMs = 400.0;
    static constexpr double kFastWindowMs = 100.0;
    static constexpr double kControlIntervalMs = 10.0;
    static constexpr double kPeakGuardLookaheadMs = 2.0;
    static constexpr int kLevelHistoryTicks = 40;   // robust level: median over 400 ms of active ticks
    static constexpr int kMinTicksBeforeRiding = 8; // ~80 ms of active level data
    static constexpr float kClipThreshold = 0.99988f; // ~ -0.001 dBFS
    static constexpr int kClipConsecutiveSamples = 3;
    static constexpr int kPeakPublishTicks = 5; // 50 ms

    /// Allocates all buffers. Returns false for invalid arguments.
    bool prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;

    /// Realtime-safe. Validates and applies settings (call once per block).
    void setSettings(const SmartGainSettings& settings) noexcept;
    const SmartGainSettings& settings() const noexcept { return settings_; }

    /// Realtime-safe in-place processing. Channels beyond the prepared count
    /// are left untouched.
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    /// Current processing latency (non-zero only while the peak guard is on).
    int latencySamples() const noexcept { return latency_.load(std::memory_order_relaxed); }
    /// Latency the stage will use when the peak guard is enabled.
    int peakGuardLatencySamples() const noexcept { return guardLookahead_; }

    /// UI side: copy the latest telemetry; returns true if it is new.
    bool readTelemetry(SmartGainTelemetry& out) noexcept { return telemetry_.read(out); }

    StaticCalibrator& calibrator() noexcept { return calibrator_; }
    const StaticCalibrator& calibrator() const noexcept { return calibrator_; }

    double sampleRate() const noexcept { return sampleRate_; }
    bool isPrepared() const noexcept { return prepared_; }

private:
    void controlTick() noexcept;
    float robustLevelDb() const noexcept;

    SmartGainSettings settings_;
    bool prepared_ = false;
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    int hopSamples_ = 480;
    int tickCounter_ = 0;

    // Detectors & meters
    dsp::RollingPower primaryIn_, fastLevel_, fastDetector_, primaryOut_;
    std::array<dsp::Biquad, kMaxChannels> detectorHpf_ {};
    dsp::TruePeakEstimator truePeak_;
    dsp::BlockMaxWindow rawPeakHold_; // 1 s of per-tick raw peaks for prediction
    // Published peaks are the max over the last 50 ms so a UI polling at any
    // rate >= 20 Hz never misses a peak between snapshots.
    dsp::BlockMaxWindow inPeakPublish_, outPeakPublish_, truePeakPublish_;
    NoiseFloorTracker noiseFloor_;
    ActivityDetector activity_;
    std::array<float, kLevelHistoryTicks> levelHistory_ {};
    int levelHistoryCount_ = 0;
    int levelHistoryPos_ = 0;
    float inactiveMs_ = 0.0f;

    // Gain
    VocalRider rider_;
    StaticCalibrator calibrator_;
    double staticCurrentDb_ = 0.0;
    double staticSlewDbPerSample_ = 0.0;
    double lastTotalDb_ = 0.0;
    float totalGain_ = 1.0f;

    // Peak guard / latency / transitions
    dsp::LookaheadPeakGuard guard_;
    int guardLookahead_ = 96;
    bool guardActive_ = false;
    std::vector<float> dryDelay_; // guardLookahead_ + 1 frames
    int dryDelayPos_ = 0;
    float fadeGain_ = 1.0f;
    float fadeStep_ = 0.0f;
    float mix_ = 1.0f;
    float mixStep_ = 0.0f;
    std::atomic<int> latency_ { 0 };

    // Per-tick accumulators
    float tickInPeak_ = 0.0f;
    float tickOutPeak_ = 0.0f;
    float tickTruePeak_ = 0.0f;
    float tickGuardMinGain_ = 1.0f;
    int clipRun_ = 0;
    bool clipEventThisTick_ = false;
    int clipHoldTicks_ = 0;
    int peakWarnHoldTicks_ = 0;
    std::uint32_t clipEvents_ = 0;
    std::uint32_t nonFinite_ = 0;

    SmartGainTelemetry snapshot_;
    TripleBuffer<SmartGainTelemetry> telemetry_;
};

} // namespace mixcore::gain
