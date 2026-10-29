#pragma once
// Noise-floor-relative vocal activity detector with confirmation and
// phrase-end hold. Runs at control rate (one update per tick).
//
//   open  threshold = max(noiseFloor + margin, absoluteMinDb)
//   close threshold = open - hysteresis
//
//   Inactive --(level > open)--> Confirming --(> open for confirmMs)--> Active
//   Active   --(level < close)--> Hold --(>= close)--> Active
//   Hold     --(below close for holdMs)--> Inactive

#include "mixcore/gain/SmartGainTelemetry.h"

#include <algorithm>

namespace mixcore::gain {

class ActivityDetector
{
public:
    void configure(float confirmMs, float holdMs, float marginDb) noexcept
    {
        confirmMs_ = confirmMs;
        holdMs_ = holdMs;
        marginDb_ = marginDb;
    }

    void reset() noexcept
    {
        state_ = ActivityState::Inactive;
        confirmAccMs_ = 0.0f;
        holdAccMs_ = 0.0f;
        activeRunMs_ = 0.0f;
    }

    ActivityState update(float levelDb, float noiseFloorDb, float tickMs) noexcept
    {
        const float open = std::max(noiseFloorDb + marginDb_, absoluteMinDb);
        const float close = open - hysteresisDb;

        switch (state_)
        {
            case ActivityState::Inactive:
            case ActivityState::Confirming:
                if (levelDb > open)
                {
                    confirmAccMs_ = (state_ == ActivityState::Inactive) ? tickMs : confirmAccMs_ + tickMs;
                    state_ = ActivityState::Confirming;
                    if (confirmAccMs_ >= confirmMs_)
                    {
                        state_ = ActivityState::Active;
                        activeRunMs_ = 0.0f;
                    }
                }
                else
                {
                    state_ = ActivityState::Inactive;
                    confirmAccMs_ = 0.0f;
                }
                break;

            case ActivityState::Active:
                activeRunMs_ += tickMs;
                if (levelDb < close)
                {
                    state_ = ActivityState::Hold;
                    holdAccMs_ = 0.0f;
                }
                break;

            case ActivityState::Hold:
                activeRunMs_ += tickMs;
                if (levelDb >= close)
                {
                    state_ = ActivityState::Active;
                }
                else
                {
                    holdAccMs_ += tickMs;
                    if (holdAccMs_ >= holdMs_)
                    {
                        state_ = ActivityState::Inactive;
                        activeRunMs_ = 0.0f;
                        confirmAccMs_ = 0.0f;
                    }
                }
                break;
        }
        return state_;
    }

    ActivityState state() const noexcept { return state_; }
    /// Time since the current phrase became Active (includes Hold), ms.
    float activeRunMs() const noexcept { return activeRunMs_; }

    float hysteresisDb = 3.0f;
    float absoluteMinDb = -70.0f;

private:
    ActivityState state_ = ActivityState::Inactive;
    float confirmMs_ = 80.0f;
    float holdMs_ = 150.0f;
    float marginDb_ = 10.0f;
    float confirmAccMs_ = 0.0f;
    float holdAccMs_ = 0.0f;
    float activeRunMs_ = 0.0f;
};

} // namespace mixcore::gain
