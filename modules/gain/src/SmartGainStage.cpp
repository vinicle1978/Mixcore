#include "mixcore/gain/SmartGainStage.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>

namespace mixcore::gain {

namespace {
constexpr double kStaticSlewDbPerSec = 240.0; // 12 dB in 50 ms: click-free, still responsive
constexpr double kBypassFadeMs = 10.0;
constexpr double kTransitionFadeMs = 5.0;
constexpr int kClipHoldTicks = 200;     // 2 s
constexpr int kPeakWarnHoldTicks = 100; // 1 s
} // namespace

bool SmartGainStage::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    prepared_ = false;
    if (!(sampleRate >= 8000.0 && sampleRate <= 768000.0) || maxBlockSize < 1 || numChannels < 1)
        return false;

    sampleRate_ = sampleRate;
    numChannels_ = std::min(numChannels, kMaxChannels);
    hopSamples_ = msToSamples(kControlIntervalMs, sampleRate_);
    const double tickSeconds = hopSamples_ / sampleRate_;

    primaryIn_.prepare(msToSamples(kPrimaryWindowMs, sampleRate_));
    primaryOut_.prepare(msToSamples(kPrimaryWindowMs, sampleRate_));
    fastLevel_.prepare(msToSamples(kFastWindowMs, sampleRate_));
    fastDetector_.prepare(msToSamples(kFastWindowMs, sampleRate_));
    for (auto& f : detectorHpf_)
        f.setHighPass(sampleRate_, 100.0, 0.70710678);

    truePeak_.prepare(numChannels_);
    rawPeakHold_.prepare(static_cast<int>(std::lround(1.0 / tickSeconds)));
    inPeakPublish_.prepare(kPeakPublishTicks);
    outPeakPublish_.prepare(kPeakPublishTicks);
    truePeakPublish_.prepare(kPeakPublishTicks);
    noiseFloor_.prepare(10, 50, tickSeconds); // 100 ms blocks, 5 s history
    calibrator_.prepare(tickSeconds);
    rider_.prepare(sampleRate_);

    guardLookahead_ = msToSamples(kPeakGuardLookaheadMs, sampleRate_);
    guard_.prepare(numChannels_, guardLookahead_, sampleRate_);
    dryDelay_.assign(static_cast<size_t>((guardLookahead_ + 1) * numChannels_), 0.0f);

    staticSlewDbPerSample_ = kStaticSlewDbPerSec / sampleRate_;
    fadeStep_ = static_cast<float>(1.0 / (kTransitionFadeMs * 0.001 * sampleRate_));
    mixStep_ = static_cast<float>(1.0 / (kBypassFadeMs * 0.001 * sampleRate_));

    prepared_ = true;
    reset();
    return true;
}

void SmartGainStage::reset() noexcept
{
    if (!prepared_)
        return;

    primaryIn_.reset();
    primaryOut_.reset();
    fastLevel_.reset();
    fastDetector_.reset();
    for (auto& f : detectorHpf_)
        f.reset();
    truePeak_.reset();
    rawPeakHold_.reset();
    inPeakPublish_.reset();
    outPeakPublish_.reset();
    truePeakPublish_.reset();
    noiseFloor_.reset();
    activity_.reset();
    rider_.reset();
    guard_.reset();
    std::fill(dryDelay_.begin(), dryDelay_.end(), 0.0f);
    dryDelayPos_ = 0;
    levelHistoryCount_ = 0;
    levelHistoryPos_ = 0;
    inactiveMs_ = 0.0f;
    tickCounter_ = 0;

    // Settled state: no ramps after reset/prepare.
    settings_.validate();
    staticCurrentDb_ = static_cast<double>(settings_.staticGainDb);
    lastTotalDb_ = staticCurrentDb_;
    totalGain_ = static_cast<float>(dbToGainD(lastTotalDb_));
    guardActive_ = settings_.peakGuardEnabled;
    latency_.store(guardActive_ ? guardLookahead_ : 0, std::memory_order_relaxed);
    fadeGain_ = 1.0f;
    mix_ = settings_.bypass ? 0.0f : 1.0f;

    tickInPeak_ = tickOutPeak_ = tickTruePeak_ = 0.0f;
    tickGuardMinGain_ = 1.0f;
    clipRun_ = 0;
    clipEventThisTick_ = false;
    clipHoldTicks_ = peakWarnHoldTicks_ = 0;
    clipEvents_ = 0;
    nonFinite_ = 0;
    snapshot_ = SmartGainTelemetry {};
}

void SmartGainStage::setSettings(const SmartGainSettings& incoming) noexcept
{
    settings_ = incoming;
    settings_.validate();

    VocalRider::Config rc;
    rc.enabled = settings_.riderEnabled;
    rc.targetDb = settings_.targetRmsDb;
    rc.strength = settings_.riderStrength;
    rc.rangeDb = settings_.riderRangeDb;
    rc.maxUpDb = settings_.riderMaxUpDb;
    rc.maxDownDb = settings_.riderMaxDownDb;
    rc.attackMs = settings_.riderAttackMs;
    rc.releaseMs = settings_.riderReleaseMs;
    rc.maxRateDbPerSec = settings_.riderMaxRateDbPerSec;
    rc.deadbandDb = settings_.deadbandDb;
    rider_.configure(rc);

    activity_.configure(settings_.activityConfirmMs, settings_.phraseHoldMs, settings_.activityMarginDb);
    guard_.setCeilingDb(settings_.peakGuardCeilingDb);
}

float SmartGainStage::robustLevelDb() const noexcept
{
    if (levelHistoryCount_ == 0)
        return kSilenceDb;
    std::array<float, kLevelHistoryTicks> sorted {};
    std::copy_n(levelHistory_.begin(), levelHistoryCount_, sorted.begin());
    const auto mid = sorted.begin() + levelHistoryCount_ / 2;
    std::nth_element(sorted.begin(), mid, sorted.begin() + levelHistoryCount_);
    return *mid;
}

void SmartGainStage::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    if (!prepared_ || channels == nullptr || numSamples <= 0)
        return;

    const int nCh = std::min(numChannels, numChannels_);
    if (nCh <= 0)
        return;

    const double targetStaticDb = static_cast<double>(settings_.staticGainDb);
    const float mixTarget = settings_.bypass ? 0.0f : 1.0f;
    const bool wantGuard = settings_.peakGuardEnabled;
    const double invCh = 1.0 / nCh;

    std::array<float, kMaxChannels> frame {};
    std::array<float, kMaxChannels> dry {};

    for (int i = 0; i < numSamples; ++i)
    {
        // ---- Input: sanitise, meter, detect --------------------------------
        double rawPower = 0.0;
        double detPower = 0.0;
        float rawPeak = 0.0f;
        for (int ch = 0; ch < nCh; ++ch)
        {
            float x = channels[ch][i];
            if (!std::isfinite(x))
            {
                x = 0.0f;
                ++nonFinite_;
            }
            frame[ch] = x;
            dry[ch] = x;
            rawPeak = std::max(rawPeak, std::abs(x));
            rawPower += static_cast<double>(x) * static_cast<double>(x);
            const float d = detectorHpf_[static_cast<size_t>(ch)].process(x);
            detPower += static_cast<double>(d) * static_cast<double>(d);
        }
        rawPower *= invCh;
        detPower *= invCh;

        if (rawPeak >= kClipThreshold)
        {
            if (++clipRun_ == kClipConsecutiveSamples)
            {
                ++clipEvents_;
                clipEventThisTick_ = true;
            }
        }
        else
        {
            clipRun_ = 0;
        }

        tickInPeak_ = std::max(tickInPeak_, rawPeak);
        primaryIn_.push(rawPower);
        fastLevel_.push(rawPower);
        fastDetector_.push(detPower);

        // ---- Gain -----------------------------------------------------------
        const double staticDiff = targetStaticDb - staticCurrentDb_;
        if (staticDiff != 0.0)
            staticCurrentDb_ += std::clamp(staticDiff, -staticSlewDbPerSample_, staticSlewDbPerSample_);

        const double totalDb = staticCurrentDb_ + rider_.nextGainDb();
        if (totalDb != lastTotalDb_)
        {
            lastTotalDb_ = totalDb;
            totalGain_ = static_cast<float>(dbToGainD(totalDb));
        }
        for (int ch = 0; ch < nCh; ++ch)
            frame[ch] *= totalGain_;

        // ---- Optional lookahead peak guard ----------------------------------
        // Dry path is always delayed through the same-length line so that a
        // latency switch only needs the short transition fade.
        float* dslot = dryDelay_.data() + static_cast<size_t>(dryDelayPos_ * numChannels_);
        const int dread = (dryDelayPos_ + 1) % (guardLookahead_ + 1);
        const float* dold = dryDelay_.data() + static_cast<size_t>(dread * numChannels_);
        for (int ch = 0; ch < nCh; ++ch)
            dslot[ch] = dry[ch];
        if (guardActive_)
        {
            for (int ch = 0; ch < nCh; ++ch)
                dry[ch] = dold[ch];
            const float g = guard_.processFrame(frame.data(), nCh);
            tickGuardMinGain_ = std::min(tickGuardMinGain_, g);
        }
        dryDelayPos_ = dread;

        // Guard on/off changes latency: fade out, switch, fade in.
        if (wantGuard != guardActive_)
        {
            fadeGain_ -= fadeStep_;
            if (fadeGain_ <= 0.0f)
            {
                fadeGain_ = 0.0f;
                guardActive_ = wantGuard;
                guard_.reset();
                latency_.store(guardActive_ ? guardLookahead_ : 0, std::memory_order_relaxed);
            }
        }
        else if (fadeGain_ < 1.0f)
        {
            fadeGain_ = std::min(1.0f, fadeGain_ + fadeStep_);
        }

        // ---- Bypass crossfade -----------------------------------------------
        if (mix_ != mixTarget)
            mix_ = mixTarget > mix_ ? std::min(mixTarget, mix_ + mixStep_) : std::max(mixTarget, mix_ - mixStep_);

        // ---- Output ---------------------------------------------------------
        double outPower = 0.0;
        for (int ch = 0; ch < nCh; ++ch)
        {
            float y = (mix_ >= 1.0f) ? frame[ch] : (mix_ <= 0.0f ? dry[ch] : dry[ch] + mix_ * (frame[ch] - dry[ch]));
            y *= fadeGain_;
            channels[ch][i] = y;
            const float a = std::abs(y);
            tickOutPeak_ = std::max(tickOutPeak_, a);
            tickTruePeak_ = std::max(tickTruePeak_, truePeak_.process(ch, y));
            outPower += static_cast<double>(y) * static_cast<double>(y);
        }
        primaryOut_.push(outPower * invCh);

        if (++tickCounter_ >= hopSamples_)
        {
            tickCounter_ = 0;
            controlTick();
        }
    }
}

void SmartGainStage::controlTick() noexcept
{
    const float tickMs = static_cast<float>(hopSamples_ * 1000.0 / sampleRate_);

    const float detectorDb = powerToDb(fastDetector_.meanPower());
    const float fastLevelDb = powerToDb(fastLevel_.meanPower());
    const float primaryInDb = powerToDb(primaryIn_.meanPower());

    noiseFloor_.update(detectorDb);
    const ActivityState state = activity_.update(detectorDb, noiseFloor_.floorDb(), tickMs);
    const bool active = state == ActivityState::Active;

    rawPeakHold_.push(tickInPeak_);
    inPeakPublish_.push(tickInPeak_);
    outPeakPublish_.push(tickOutPeak_);
    truePeakPublish_.push(tickTruePeak_);

    if (active)
    {
        levelHistory_[static_cast<size_t>(levelHistoryPos_)] = fastLevelDb;
        levelHistoryPos_ = (levelHistoryPos_ + 1) % kLevelHistoryTicks;
        levelHistoryCount_ = std::min(levelHistoryCount_ + 1, kLevelHistoryTicks);
    }

    // Calibration: only windows that lie entirely inside an active phrase.
    const bool validWindow = active && primaryIn_.isFilled()
                             && activity_.activeRunMs() >= static_cast<float>(kPrimaryWindowMs);
    calibrator_.onTick(validWindow, primaryInDb, tickInPeak_);

    // Rider: level is measured on the raw input and referred to post-static
    // using the *target* static gain, so the rider never reacts to (or alters)
    // static gain changes.
    const float staticDb = settings_.staticGainDb;
    const float rawLevelDb = robustLevelDb();
    const float levelPostStatic = rawLevelDb + staticDb;
    const bool riding = active && levelHistoryCount_ >= kMinTicksBeforeRiding;
    const float predictedPeakDb = gainToDb(rawPeakHold_.max()) + staticDb;
    const float headroomDb = settings_.peakRestrictDb - predictedPeakDb;
    inactiveMs_ = (state == ActivityState::Inactive) ? inactiveMs_ + tickMs : 0.0f;
    const float idleDecayDb = inactiveMs_ >= settings_.idleReleaseDelayMs
                                  ? settings_.idleReleaseDbPerSec * tickMs * 0.001f
                                  : 0.0f;
    rider_.updateTarget(riding, levelPostStatic, headroomDb, idleDecayDb);

    // Holds & flags
    if (clipEventThisTick_)
        clipHoldTicks_ = kClipHoldTicks;
    else if (clipHoldTicks_ > 0)
        --clipHoldTicks_;

    const float outPeakDb = gainToDb(tickOutPeak_);
    if (outPeakDb > settings_.peakWarningDb)
        peakWarnHoldTicks_ = kPeakWarnHoldTicks;
    else if (peakWarnHoldTicks_ > 0)
        --peakWarnHoldTicks_;

    // ---- Publish -------------------------------------------------------------
    SmartGainTelemetry& t = snapshot_;
    ++t.sequence;
    t.inputRmsDb = primaryInDb;
    t.outputRmsDb = powerToDb(primaryOut_.meanPower());
    t.inputPeakDb = gainToDb(inPeakPublish_.max());
    t.outputPeakDb = gainToDb(outPeakPublish_.max());
    t.outputTruePeakDb = gainToDb(truePeakPublish_.max());
    t.noiseFloorDb = noiseFloor_.floorDb();
    t.staticGainDb = static_cast<float>(staticCurrentDb_);
    t.riderGainDb = rider_.gainDb();
    t.riderTargetDb = rider_.desiredDb();
    t.riderRestrictionDb = rider_.restrictionDb();
    t.peakGuardReductionDb = -gainToDb(tickGuardMinGain_);
    t.levelEstimateDb = levelHistoryCount_ > 0 ? levelPostStatic : kSilenceDb;
    t.targetErrorValid = riding;
    t.targetErrorDb = riding ? settings_.targetRmsDb - (levelPostStatic + rider_.gainDb()) : 0.0f;
    t.activity = state;
    t.riderEnabled = settings_.riderEnabled;
    t.inputClipping = clipHoldTicks_ > 0;
    t.inputClipEvents = clipEvents_;
    t.peakWarning = peakWarnHoldTicks_ > 0;
    t.peakRestrictionActive = rider_.restrictionDb() > 0.01f;
    t.peakGuardEnabled = guardActive_;
    t.peakGuardActive = t.peakGuardReductionDb > 0.01f;
    t.bypassed = settings_.bypass;
    t.nonFiniteInputSamples = nonFinite_;
    t.calibration = calibrator_.state();
    t.calibrationProgress = std::min(1.0f, calibrator_.progress());
    t.latencySamples = latency_.load(std::memory_order_relaxed);
    t.sampleRate = sampleRate_;
    telemetry_.write(t);

    tickInPeak_ = tickOutPeak_ = tickTruePeak_ = 0.0f;
    tickGuardMinGain_ = 1.0f;
    clipEventThisTick_ = false;
}

} // namespace mixcore::gain
