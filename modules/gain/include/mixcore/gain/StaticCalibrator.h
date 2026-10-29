#pragma once
// Static gain calibration.
//
// Thread model:
//   * Control/UI thread: requestStart(), cancel(), state(), computeResult().
//   * Audio thread:      onTick() once per control tick.
// Hand-off is via one atomic state word. The audio thread only writes the
// sample store while state == Collecting and publishes Ready/TimedOut with
// release ordering; computeResult() reads only after observing that with
// acquire ordering, so there is no data race and no lock.
//
// Measurement: the 400 ms primary RMS of the *raw* input (before static gain)
// is recorded each tick while the vocal has been continuously Active for at
// least one full window (so the window contains only vocal). The
// representative level is the median of those windows.

#include "mixcore/gain/SmartGainTelemetry.h"

#include <atomic>
#include <string>
#include <vector>

namespace mixcore::gain {

struct CalibrationResult
{
    bool valid = false;
    int windowsUsed = 0;
    float measuredMedianDb = -150.0f;   ///< Raw active RMS, dBFS.
    float spreadDb = 0.0f;              ///< Inter-quartile range of window levels.
    float maxInputPeakDb = -150.0f;     ///< Raw sample peak seen while calibrating.
    float rawGainDb = 0.0f;             ///< target - measured, unconstrained.
    float recommendedGainDb = 0.0f;     ///< Constrained to the recommendation range.
    bool clampedToRange = false;
    float predictedPeakDb = -150.0f;    ///< maxInputPeak + recommended.
    bool peakConflict = false;          ///< predictedPeak > restrict threshold.
    float safeGainDb = 0.0f;            ///< Highest gain respecting the restrict threshold.
    bool lowConfidence = false;         ///< Spread too wide or too few windows.
    std::string summary;                ///< Plain-language explanation for the UI.
};

class StaticCalibrator
{
public:
    static constexpr float kRecommendRangeDb = 12.0f;

    /// Allocates the window store. Non-realtime.
    void prepare(double tickSeconds, double maxCollectSeconds = 30.0, double timeoutSeconds = 90.0);

    // ---- control thread ----
    void requestStart(double requiredActiveSeconds = 6.0) noexcept;
    void cancel() noexcept;
    CalibrationState state() const noexcept { return state_.load(std::memory_order_acquire); }
    float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }
    /// Valid only when state() is Ready or TimedOut. Allocates (control thread only).
    CalibrationResult computeResult(float targetRmsDb, float restrictDb) const;

    // ---- audio thread ----
    void onTick(bool validWindow, float primaryRmsDb, float tickInputPeak) noexcept;

private:
    std::vector<float> windows_;
    int count_ = 0;
    int requiredWindows_ = 600;
    int ticksElapsed_ = 0;
    int timeoutTicks_ = 9000;
    float maxPeak_ = 0.0f;
    double tickSeconds_ = 0.01;
    std::atomic<CalibrationState> state_ { CalibrationState::Idle };
    std::atomic<int> pendingRequired_ { 600 };
    std::atomic<float> progress_ { 0.0f };
};

} // namespace mixcore::gain
