#include "mixcore/dsp/LookaheadPeakGuard.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mixcore::dsp {

void LookaheadPeakGuard::prepare(int numChannels, int lookaheadSamples, double sampleRate)
{
    numChannels_ = std::max(1, numChannels);
    lookahead_ = std::max(1, lookaheadSamples);
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

    delayLength_ = lookahead_ + 1;
    delay_.assign(static_cast<size_t>(delayLength_ * numChannels_), 0.0f);

    dqCapacity_ = lookahead_ + 2;
    dqValue_.assign(static_cast<size_t>(dqCapacity_), 1.0f);
    dqTime_.assign(static_cast<size_t>(dqCapacity_), 0);

    box_.assign(static_cast<size_t>(lookahead_), 1.0);
    setReleaseMs(80.0f);
    reset();
}

void LookaheadPeakGuard::reset() noexcept
{
    std::fill(delay_.begin(), delay_.end(), 0.0f);
    delayPos_ = 0;
    dqHead_ = 0;
    dqCount_ = 0;
    time_ = 0;
    std::fill(box_.begin(), box_.end(), 1.0);
    boxSum_ = static_cast<double>(lookahead_);
    boxPos_ = 0;
    envelope_ = 1.0f;
}

void LookaheadPeakGuard::setCeilingDb(float ceilingDb) noexcept
{
    ceiling_ = dbToGain(clampFinite(ceilingDb, -40.0f, 0.0f, -3.0f));
}

void LookaheadPeakGuard::setReleaseMs(float releaseMs) noexcept
{
    releaseCoef_ = timeConstantCoef(static_cast<double>(clampFinite(releaseMs, 1.0f, 5000.0f, 80.0f)) * 0.001, sampleRate_);
}

float LookaheadPeakGuard::processFrame(float* frame, int numChannels) noexcept
{
    const int nCh = std::min(numChannels, numChannels_);

    // 1. Required gain for the incoming frame.
    float peak = 0.0f;
    for (int ch = 0; ch < nCh; ++ch)
        peak = std::max(peak, std::abs(frame[ch]));
    const float required = peak > ceiling_ ? ceiling_ / peak : 1.0f;

    // 2. Sliding minimum over the last (L + 1) required-gain values.
    while (dqCount_ > 0)
    {
        const int back = (dqHead_ + dqCount_ - 1) % dqCapacity_;
        if (dqValue_[static_cast<size_t>(back)] >= required)
            --dqCount_;
        else
            break;
    }
    {
        const int slot = (dqHead_ + dqCount_) % dqCapacity_;
        dqValue_[static_cast<size_t>(slot)] = required;
        dqTime_[static_cast<size_t>(slot)] = time_;
        ++dqCount_;
    }
    while (dqTime_[static_cast<size_t>(dqHead_)] <= time_ - (lookahead_ + 1))
    {
        dqHead_ = (dqHead_ + 1) % dqCapacity_;
        --dqCount_;
    }
    const float held = dqValue_[static_cast<size_t>(dqHead_)];
    ++time_;

    // 3. Box smoothing over L values.
    boxSum_ += static_cast<double>(held) - box_[static_cast<size_t>(boxPos_)];
    box_[static_cast<size_t>(boxPos_)] = static_cast<double>(held);
    if (++boxPos_ == lookahead_)
    {
        boxPos_ = 0;
        boxSum_ = std::accumulate(box_.begin(), box_.end(), 0.0);
    }
    const float smoothed = static_cast<float>(boxSum_ / lookahead_);

    // 4. Instant attack (already ramped by the box), smooth release.
    if (smoothed < envelope_)
        envelope_ = smoothed;
    else
        envelope_ = smoothed + (envelope_ - smoothed) * releaseCoef_;

    // 5. Delay audio by L samples and apply gain.
    float* slot = delay_.data() + static_cast<size_t>(delayPos_ * numChannels_);
    const int readPos = (delayPos_ + 1) % delayLength_; // oldest == t - L
    const float* oldest = delay_.data() + static_cast<size_t>(readPos * numChannels_);
    for (int ch = 0; ch < nCh; ++ch)
        slot[ch] = frame[ch];
    for (int ch = 0; ch < nCh; ++ch)
        frame[ch] = oldest[ch] * envelope_;
    delayPos_ = readPos;

    return envelope_;
}

} // namespace mixcore::dsp
