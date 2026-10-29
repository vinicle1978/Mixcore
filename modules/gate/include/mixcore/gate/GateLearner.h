#pragma once
// "Learn threshold": listens to the detector for a few seconds while the
// source plays normally (with its natural gaps) and suggests a threshold that
// separates the source from the bleed/noise in its mic.
//
// The tick levels are split into two groups with Otsu's method (the split
// that maximises the difference between the groups), so the result does not
// depend on how much of the time the source was playing.
//
// Sustained sources (vocals, speech; RMS detector): the threshold is placed
// 40 % of the way from the bleed level to the typical source level: low
// enough to catch quiet words, high enough to stay shut on bleed.
//
// Percussive sources (drums; peak detector): the source level is the hit
// PEAK (90th percentile of the loud group) and the threshold sits 18 dB
// below it (but never closer than 6 dB to the bleed). Otherwise a well
// side-chain-filtered drum, whose bleed is very quiet, would get a threshold
// so low that the drum's own ring keeps the gate open between hits.
//
// Thread model is the same as StaticCalibrator: audio thread calls onTick();
// UI thread requests, cancels and computes the result after Ready.

#include "mixcore/gate/GateTelemetry.h"

#include <atomic>
#include <string>
#include <vector>

namespace mixcore::gate {

struct LearnResult
{
    bool valid = false;
    float bleedDb = -150.0f;         ///< Typical level between hits/phrases.
    float sourceDb = -150.0f;        ///< Typical level of the source itself.
    float separationDb = 0.0f;
    float suggestedThresholdDb = -40.0f;
    float suggestedHysteresisDb = 6.0f;
    enum class Confidence { None, Low, Good } confidence = Confidence::None;
    std::string summary;
};

class GateLearner
{
public:
    void prepare(double tickSeconds, double maxSeconds = 20.0);

    // ---- control thread ----
    void requestStart(double seconds = 8.0) noexcept;
    void cancel() noexcept;
    LearnState state() const noexcept { return state_.load(std::memory_order_acquire); }
    float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }
    LearnResult computeResult(bool percussive) const; // valid after Ready; allocates

    // ---- audio thread ----
    void onTick(float detectorDb) noexcept;

    /// Pure analysis, exposed for testing.
    static LearnResult analyse(std::vector<float> levels, bool percussive);

private:
    std::vector<float> levels_;
    int count_ = 0;
    int required_ = 800;
    double tickSeconds_ = 0.01;
    std::atomic<LearnState> state_ { LearnState::Idle };
    std::atomic<int> pendingRequired_ { 800 };
    std::atomic<float> progress_ { 0.0f };
};

} // namespace mixcore::gate
