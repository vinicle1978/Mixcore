#include "mixcore/gate/NoiseGate.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>

namespace mixcore::gate {

namespace {
constexpr double kPeakReleaseMs = 10.0;
constexpr double kRmsWindowMs = 10.0;
constexpr double kSlowEnvMs = 50.0;
constexpr double kEnableFadeMs = 10.0;
constexpr double kLatencyFadeMs = 5.0;
constexpr float kFloorDb = -100.0f;
constexpr double kButterQ1 = 0.54119610; // 4th-order Butterworth section Qs
constexpr double kButterQ2 = 1.30656296;
} // namespace

bool NoiseGate::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    prepared_ = false;
    if (!(sampleRate >= 8000.0 && sampleRate <= 768000.0) || maxBlockSize < 1 || numChannels < 1)
        return false;

    sampleRate_ = sampleRate;
    numChannels_ = std::min(numChannels, kMaxChannels);
    hopSamples_ = msToSamples(kControlIntervalMs, sampleRate_);

    delayCapacity_ = static_cast<int>(std::ceil(kMaxLookaheadMs * 0.001 * sampleRate_)) + 1;
    delay_.assign(static_cast<size_t>(delayCapacity_ * numChannels_), 0.0f);

    peakReleaseCoef_ = timeConstantCoef(kPeakReleaseMs * 0.001, sampleRate_);
    rmsCoef_ = timeConstantCoef(kRmsWindowMs * 0.001, sampleRate_);
    slowCoef_ = timeConstantCoef(kSlowEnvMs * 0.001, sampleRate_);
    fadeStep_ = static_cast<float>(1.0 / (kLatencyFadeMs * 0.001 * sampleRate_));
    mixStep_ = static_cast<float>(1.0 / (kEnableFadeMs * 0.001 * sampleRate_));

    learner_.prepare(hopSamples_ / sampleRate_);
    outPower_.prepare(msToSamples(400.0, sampleRate_));
    outPeakPublish_.prepare(5);

    prepared_ = true;
    designedHpf_ = designedLpf_ = -1.0f; // force design
    updateCoefficients();
    reset();
    return true;
}

void NoiseGate::reset() noexcept
{
    if (!prepared_)
        return;
    for (auto& pair : hpf_)
        for (auto& f : pair)
            f.reset();
    for (auto& pair : lpf_)
        for (auto& f : pair)
            f.reset();
    std::fill(delay_.begin(), delay_.end(), 0.0f);
    delayPos_ = 0;
    peakEnv_ = 0.0f;
    rmsPower_ = 0.0;
    slowEnv_ = 0.0f;
    state_ = GateState::Closed;
    holdCounter_ = 0;
    // Start settled: closed gate already at full attenuation, no fade-ins.
    gainDb_ = -static_cast<double>(settings_.rangeDb);
    gain_ = static_cast<float>(dbToGainD(gainDb_));
    lookahead_ = wantLookahead_;
    latency_.store(lookahead_, std::memory_order_relaxed);
    fade_ = 1.0f;
    mix_ = (settings_.enabled && !bypassed_) ? 1.0f : 0.0f;
    tickCounter_ = 0;
    outPower_.reset();
    outPeakPublish_.reset();
    tickOutPeak_ = 0.0f;
    tickDetMax_ = 0.0f;
    tickMinGainDb_ = 0.0f;
    opensTotal_ = 0;
    opensPerTick_.fill(0);
    opensPos_ = 0;
    opensThisTick_ = 0;
    closedTicks_ = 0;
    snapshot_ = GateTelemetry {};
}

void NoiseGate::setSettings(const GateSettings& incoming) noexcept
{
    settings_ = incoming;
    settings_.validate();
    if (prepared_)
        updateCoefficients();
}

void NoiseGate::updateCoefficients() noexcept
{
    const double sr = sampleRate_;
    useHpf_ = settings_.sidechainHpfHz > 20.5f;
    useLpf_ = settings_.sidechainLpfHz < 19999.0f && settings_.sidechainLpfHz < 0.45f * static_cast<float>(sr);
    if (std::abs(settings_.sidechainHpfHz - designedHpf_) > 1.0e-3f)
    {
        designedHpf_ = settings_.sidechainHpfHz;
        for (auto& pair : hpf_)
        {
            pair[0].setHighPass(sr, static_cast<double>(designedHpf_), kButterQ1);
            pair[1].setHighPass(sr, static_cast<double>(designedHpf_), kButterQ2);
        }
    }
    if (std::abs(settings_.sidechainLpfHz - designedLpf_) > 1.0e-3f)
    {
        designedLpf_ = settings_.sidechainLpfHz;
        const double fc = std::min(static_cast<double>(designedLpf_), 0.45 * sr);
        for (auto& pair : lpf_)
        {
            pair[0].setLowPass(sr, fc, kButterQ1);
            pair[1].setLowPass(sr, fc, kButterQ2);
        }
    }

    const double range = std::max(1.0, static_cast<double>(settings_.rangeDb));
    openStepDb_ = range / std::max(1.0, static_cast<double>(settings_.attackMs) * 0.001 * sr);
    closeStepDb_ = range / std::max(1.0, static_cast<double>(settings_.releaseMs) * 0.001 * sr);
    holdSamples_ = static_cast<int>(std::lround(static_cast<double>(settings_.holdMs) * 0.001 * sr));
    wantLookahead_ = std::min(delayCapacity_ - 1,
                              static_cast<int>(std::lround(static_cast<double>(settings_.lookaheadMs) * 0.001 * sr)));
}

void NoiseGate::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    if (!prepared_ || channels == nullptr || numSamples <= 0)
        return;
    const int nCh = std::min(numChannels, numChannels_);
    if (nCh <= 0)
        return;

    const float thrLin = dbToGain(settings_.thresholdDb);
    const float closeLin = dbToGain(settings_.thresholdDb - settings_.hysteresisDb);
    const float transientRatio = dbToGain(settings_.transientDb);
    const bool hard = settings_.isHardGate();
    const double range = static_cast<double>(settings_.rangeDb);
    const double ratioMinus1 = static_cast<double>(settings_.ratio) - 1.0;
    const float mixTarget = (settings_.enabled && !bypassed_) ? 1.0f : 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // ---- Side-chain detector --------------------------------------------
        float a = 0.0f;
        for (int ch = 0; ch < nCh; ++ch)
        {
            float s = channels[ch][i];
            if (!std::isfinite(s))
                s = 0.0f;
            if (useHpf_)
            {
                auto& f = hpf_[static_cast<size_t>(ch)];
                s = f[1].process(f[0].process(s));
            }
            if (useLpf_)
            {
                auto& f = lpf_[static_cast<size_t>(ch)];
                s = f[1].process(f[0].process(s));
            }
            a = std::max(a, std::abs(s));
        }
        float det;
        if (settings_.rmsDetector)
        {
            const double p = static_cast<double>(a) * static_cast<double>(a);
            rmsPower_ = p + (rmsPower_ - p) * static_cast<double>(rmsCoef_);
            det = static_cast<float>(std::sqrt(std::max(0.0, rmsPower_)));
        }
        else
        {
            peakEnv_ = std::max(a, peakEnv_ * peakReleaseCoef_);
            det = peakEnv_;
        }
        const float slowBefore = slowEnv_; // slow (50 ms) envelope of the detector, linear
        slowEnv_ = det + (slowEnv_ - det) * slowCoef_;
        tickDetMax_ = std::max(tickDetMax_, det);

        // ---- State machine ---------------------------------------------------
        switch (state_)
        {
            case GateState::Closed:
            {
                const bool loudEnough = det >= thrLin;
                const bool sharpRise = !settings_.transientTrigger || det >= std::max(slowBefore, 1.0e-6f) * transientRatio;
                if (loudEnough && sharpRise)
                {
                    state_ = GateState::Open;
                    ++opensTotal_;
                    if (opensThisTick_ < 255)
                        ++opensThisTick_;
                }
                break;
            }
            case GateState::Open:
                if (det < closeLin)
                {
                    state_ = GateState::Hold;
                    holdCounter_ = holdSamples_;
                }
                break;
            case GateState::Hold:
                if (det >= closeLin)
                    state_ = GateState::Open;
                else if (--holdCounter_ <= 0)
                    state_ = GateState::Closed;
                break;
        }

        // ---- Gain computer ---------------------------------------------------
        double target = 0.0;
        if (state_ == GateState::Closed)
        {
            if (hard)
                target = -range;
            else
            {
                const double below = static_cast<double>(settings_.thresholdDb) - static_cast<double>(gainToDb(det));
                target = -std::min(range, std::max(0.0, below * ratioMinus1));
            }
        }
        if (target > gainDb_)
            gainDb_ = std::min(target, gainDb_ + openStepDb_);
        else if (target < gainDb_)
            gainDb_ = std::max(target, gainDb_ - closeStepDb_);
        gain_ = static_cast<float>(dbToGainD(std::max(gainDb_, static_cast<double>(kFloorDb))));
        tickMinGainDb_ = std::min(tickMinGainDb_, static_cast<float>(gainDb_));

        // ---- Lookahead delay + output ---------------------------------------
        float* slot = delay_.data() + static_cast<size_t>(delayPos_ * numChannels_);
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float x = channels[ch][i];
            slot[ch] = std::isfinite(x) ? x : 0.0f;
        }
        const int readPos = (delayPos_ - lookahead_ + delayCapacity_) % delayCapacity_;
        const float* delayed = delay_.data() + static_cast<size_t>(readPos * numChannels_);
        delayPos_ = (delayPos_ + 1) % delayCapacity_;

        if (wantLookahead_ != lookahead_)
        {
            fade_ -= fadeStep_;
            if (fade_ <= 0.0f)
            {
                fade_ = 0.0f;
                lookahead_ = wantLookahead_;
                latency_.store(lookahead_, std::memory_order_relaxed);
            }
        }
        else if (fade_ < 1.0f)
        {
            fade_ = std::min(1.0f, fade_ + fadeStep_);
        }

        if (mix_ != mixTarget)
            mix_ = mixTarget > mix_ ? std::min(mixTarget, mix_ + mixStep_) : std::max(mixTarget, mix_ - mixStep_);

        const float g = (mix_ >= 1.0f ? gain_ : (1.0f - mix_) + mix_ * gain_) * fade_;
        double outP = 0.0;
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float y = delayed[ch] * g;
            channels[ch][i] = y;
            tickOutPeak_ = std::max(tickOutPeak_, std::abs(y));
            outP += static_cast<double>(y) * static_cast<double>(y);
        }
        outPower_.push(outP / nCh);

        if (++tickCounter_ >= hopSamples_)
        {
            tickCounter_ = 0;
            controlTick();
        }
    }
}

void NoiseGate::controlTick() noexcept
{
    const float detDb = gainToDb(tickDetMax_);
    learner_.onTick(detDb);

    opensPerTick_[static_cast<size_t>(opensPos_)] = opensThisTick_;
    opensPos_ = (opensPos_ + 1) % static_cast<int>(opensPerTick_.size());
    unsigned perSecond = 0;
    for (auto c : opensPerTick_)
        perSecond += c;

    closedTicks_ = (state_ == GateState::Closed && opensThisTick_ == 0) ? closedTicks_ + 1 : 0;

    GateTelemetry& t = snapshot_;
    ++t.sequence;
    t.detectorDb = detDb;
    t.thresholdDb = settings_.thresholdDb;
    t.closeThresholdDb = settings_.thresholdDb - settings_.hysteresisDb;
    t.gainReductionDb = mix_ > 0.0f ? -tickMinGainDb_ * mix_ : 0.0f;
    outPeakPublish_.push(tickOutPeak_);
    t.outputRmsDb = powerToDb(outPower_.meanPower());
    t.outputPeakDb = gainToDb(outPeakPublish_.max());
    t.state = state_;
    t.enabled = settings_.enabled && !bypassed_;
    t.hardGate = settings_.isHardGate();
    t.opensTotal = opensTotal_;
    t.opensLastSecond = static_cast<std::uint16_t>(perSecond);
    t.closedSeconds = static_cast<float>(closedTicks_) * static_cast<float>(kControlIntervalMs) * 0.001f;
    t.learn = learner_.state();
    t.learnProgress = std::min(1.0f, learner_.progress());
    t.latencySamples = latency_.load(std::memory_order_relaxed);
    telemetry_.write(t);

    tickDetMax_ = 0.0f;
    tickOutPeak_ = 0.0f;
    tickMinGainDb_ = static_cast<float>(gainDb_);
    opensThisTick_ = 0;
}

} // namespace mixcore::gate
