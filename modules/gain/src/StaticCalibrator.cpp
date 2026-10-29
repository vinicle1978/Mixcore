#include "mixcore/gain/StaticCalibrator.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mixcore::gain {

void StaticCalibrator::prepare(double tickSeconds, double maxCollectSeconds, double timeoutSeconds)
{
    tickSeconds_ = tickSeconds > 0.0 ? tickSeconds : 0.01;
    windows_.assign(static_cast<size_t>(std::max(1.0, maxCollectSeconds / tickSeconds_)), 0.0f);
    timeoutTicks_ = static_cast<int>(timeoutSeconds / tickSeconds_);
    count_ = 0;
    state_.store(CalibrationState::Idle, std::memory_order_release);
    progress_.store(0.0f);
}

void StaticCalibrator::requestStart(double requiredActiveSeconds) noexcept
{
    const int required = static_cast<int>(std::lround(std::clamp(requiredActiveSeconds, 1.0, 30.0) / tickSeconds_));
    pendingRequired_.store(std::clamp(required, 1, static_cast<int>(windows_.size())), std::memory_order_relaxed);
    progress_.store(0.0f, std::memory_order_relaxed);
    state_.store(CalibrationState::Requested, std::memory_order_release);
}

void StaticCalibrator::cancel() noexcept
{
    state_.store(CalibrationState::Idle, std::memory_order_release);
    progress_.store(0.0f, std::memory_order_relaxed);
}

void StaticCalibrator::onTick(bool validWindow, float primaryRmsDb, float tickInputPeak) noexcept
{
    auto s = state_.load(std::memory_order_acquire);
    if (s == CalibrationState::Requested)
    {
        count_ = 0;
        ticksElapsed_ = 0;
        maxPeak_ = 0.0f;
        requiredWindows_ = pendingRequired_.load(std::memory_order_relaxed);
        // Only move to Collecting if nobody changed the state meanwhile.
        if (!state_.compare_exchange_strong(s, CalibrationState::Collecting, std::memory_order_acq_rel))
            return;
        s = CalibrationState::Collecting;
    }
    if (s != CalibrationState::Collecting)
        return;

    ++ticksElapsed_;
    maxPeak_ = std::max(maxPeak_, tickInputPeak);
    if (validWindow && std::isfinite(primaryRmsDb) && count_ < static_cast<int>(windows_.size()))
        windows_[static_cast<size_t>(count_++)] = primaryRmsDb;

    progress_.store(static_cast<float>(count_) / static_cast<float>(requiredWindows_), std::memory_order_relaxed);

    if (count_ >= requiredWindows_)
        state_.compare_exchange_strong(s, CalibrationState::Ready, std::memory_order_acq_rel);
    else if (ticksElapsed_ >= timeoutTicks_)
        state_.compare_exchange_strong(s, CalibrationState::TimedOut, std::memory_order_acq_rel);
}

CalibrationResult StaticCalibrator::computeResult(float targetRmsDb, float restrictDb) const
{
    CalibrationResult r;
    const auto s = state();
    if (s != CalibrationState::Ready && s != CalibrationState::TimedOut)
    {
        r.summary = "Calibration has not finished.";
        return r;
    }

    std::vector<float> values(windows_.begin(), windows_.begin() + count_);
    r.windowsUsed = count_;
    r.maxInputPeakDb = gainToDb(maxPeak_);

    const int minimumWindows = static_cast<int>(1.0 / tickSeconds_); // 1 s of active vocal
    if (count_ < minimumWindows)
    {
        std::ostringstream msg;
        msg << "Not enough active vocal was detected (" << count_ * tickSeconds_
            << " s). Sing or speak at performance level and try again; "
               "if the meter shows signal but this keeps failing, the input may be too close to the noise floor.";
        r.summary = msg.str();
        return r;
    }

    auto quantile = [&values](double q) {
        auto v = values;
        const size_t k = static_cast<size_t>(std::clamp(q, 0.0, 1.0) * (v.size() - 1));
        std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
        return v[k];
    };

    r.valid = true;
    r.measuredMedianDb = quantile(0.5);
    r.spreadDb = quantile(0.75) - quantile(0.25);
    r.rawGainDb = targetRmsDb - r.measuredMedianDb;
    r.recommendedGainDb = std::clamp(r.rawGainDb, -kRecommendRangeDb, kRecommendRangeDb);
    r.clampedToRange = r.recommendedGainDb != r.rawGainDb;
    r.predictedPeakDb = r.maxInputPeakDb + r.recommendedGainDb;
    r.safeGainDb = std::clamp(restrictDb - r.maxInputPeakDb, -18.0f, 18.0f);
    r.peakConflict = r.predictedPeakDb > restrictDb;
    r.lowConfidence = r.spreadDb > 10.0f || s == CalibrationState::TimedOut;

    std::ostringstream msg;
    msg.setf(std::ios::fixed);
    msg.precision(1);
    msg << "Measured active level " << r.measuredMedianDb << " dBFS RMS (median of " << count_
        << " windows). Suggested static gain " << (r.recommendedGainDb >= 0 ? "+" : "") << r.recommendedGainDb
        << " dB to reach " << targetRmsDb << " dBFS RMS.";
    if (r.clampedToRange)
        msg << " The full correction (" << r.rawGainDb
            << " dB) is outside the +/-12 dB recommendation range: adjust the preamp gain on your interface first.";
    if (r.peakConflict)
        msg << " Warning: with this gain the loudest peaks would reach " << r.predictedPeakDb
            << " dBFS, above the " << restrictDb << " dBFS safety threshold. A safer setting is "
            << r.safeGainDb << " dB, or reduce the peaks at the source.";
    if (r.spreadDb > 10.0f)
        msg << " Confidence is low: the level varied by " << r.spreadDb
            << " dB between windows (for example quiet verses and loud choruses). Recalibrate with a passage at "
               "your typical level, or set static gain by hand.";
    if (s == CalibrationState::TimedOut)
        msg << " Calibration timed out before collecting the requested amount of vocal, so the result is based on "
            << count_ * tickSeconds_ << " s only.";
    if (r.maxInputPeakDb > -0.1f)
        msg << " The input reached full scale during calibration: lower the preamp gain. MixCore cannot repair clipping that happened before it.";
    r.summary = msg.str();
    return r;
}

} // namespace mixcore::gain
