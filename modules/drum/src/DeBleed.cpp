#include "mixcore/drum/DeBleed.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>

namespace mixcore::drum {

namespace {
constexpr double kEnvReleaseMs = 20.0;
constexpr double kSlowMs = 50.0;
constexpr double kOpenMs = 0.3;
constexpr double kCloseMs = 2.0;
constexpr double kFadeMs = 10.0;
constexpr double kLearnWindowMs = 6.0;
constexpr double kRefractoryMs = 40.0;
constexpr float kHitFloor = 0.001f;       // -60 dBFS body: quieter "hits" are ignored for learning
constexpr float kOnsetRatio = 2.0f;       // body jumps 6 dB above its 50 ms average
constexpr float kHitRelative = 0.25f;    // -12 dB re. recent loudest hit
constexpr double kHitLevelMs = 3000.0;
constexpr double kBleedMs = 150.0;
constexpr float kLearnDown = 0.4f;
constexpr float kLearnUp = 0.1f;
constexpr float kOutlierLog = 0.3f;       // hits > 6 dB brighter than learned are ignored
constexpr float kMinLogK = -3.0f;         // -60 dB
constexpr float kMaxLogK = 0.5f;          // +10 dB
constexpr double kButterQ = 0.70710678;
} // namespace

bool DeBleed::prepare(double sampleRate, int, int numChannels)
{
    prepared_ = false;
    if (!(sampleRate >= 8000.0 && sampleRate <= 768000.0) || numChannels < 1)
        return false;
    sampleRate_ = sampleRate;
    numChannels_ = std::min(numChannels, kMaxChannels);
    envRelease_ = timeConstantCoef(kEnvReleaseMs * 0.001, sampleRate_);
    slowCoef_ = timeConstantCoef(kSlowMs * 0.001, sampleRate_);
    hitLevelCoef_ = timeConstantCoef(kHitLevelMs * 0.001, sampleRate_);
    bleedCoef_ = timeConstantCoef(kBleedMs * 0.001, sampleRate_);
    openCoef_ = timeConstantCoef(kOpenMs * 0.001, sampleRate_);
    closeCoef_ = timeConstantCoef(kCloseMs * 0.001, sampleRate_);
    mixStep_ = static_cast<float>(1.0 / (kFadeMs * 0.001 * sampleRate_));
    windowSamples_ = msToSamples(kLearnWindowMs, sampleRate_);
    refractorySamples_ = msToSamples(kRefractoryMs, sampleRate_);
    hop_ = msToSamples(10.0, sampleRate_);
    prepared_ = true;
    designedCrossover_ = -1.0f;
    design();
    reset();
    return true;
}

void DeBleed::reset() noexcept
{
    if (!prepared_)
        return;
    for (auto& ch : lp_)
        for (auto& f : ch)
            f.reset();
    for (auto& ch : hp_)
        for (auto& f : ch)
            f.reset();
    bodyEnv_ = highEnv_ = allowed_ = slowBody_ = hitLevel_ = bleed_ = 0.0f;
    logK_ = kMaxLogK;
    kLin_ = std::pow(10.0f, kMaxLogK);
    refractory_ = windowLeft_ = 0;
    winBodyMax_ = winHighMax_ = 0.0f;
    hits_ = 0;
    gainDb_ = 0.0f;
    mix_ = (settings_.debleedEnabled && !bypassed_) ? 1.0f : 0.0f;
    tick_ = 0;
    tickMaxRed_ = tickBody_ = tickHigh_ = 0.0f;
    snap_ = DeBleedTelemetry {};
}

void DeBleed::setSettings(const DrumSettings& s) noexcept
{
    settings_ = s;
    settings_.validate();
    if (prepared_)
        design();
}

void DeBleed::design() noexcept
{
    lengthCoef_ = timeConstantCoef(static_cast<double>(settings_.debleedLengthMs) * 0.001, sampleRate_);
    const float fc = std::min(settings_.debleedCrossoverHz, 0.45f * static_cast<float>(sampleRate_));
    if (std::abs(fc - designedCrossover_) > 1.0e-3f)
    {
        designedCrossover_ = fc;
        for (auto& ch : lp_)
            for (auto& f : ch)
                f.setLowPass(sampleRate_, static_cast<double>(fc), kButterQ);
        for (auto& ch : hp_)
            for (auto& f : ch)
                f.setHighPass(sampleRate_, static_cast<double>(fc), kButterQ);
    }
}

void DeBleed::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    if (!prepared_ || channels == nullptr || numSamples <= 0)
        return;
    const int nCh = std::min(numChannels, numChannels_);
    if (nCh <= 0)
        return;

    if (resetLearning_.exchange(false, std::memory_order_acq_rel))
    {
        logK_ = kMaxLogK;
        kLin_ = std::pow(10.0f, kMaxLogK);
        hits_ = 0;
    }

    const float mixTarget = (settings_.debleedEnabled && !bypassed_) ? 1.0f : 0.0f;
    const float floorDb = -settings_.maxReductionDb();
    const float tolerance = dbToGain(settings_.debleedToleranceDb);

    // Fully off and settled: pass through bit-exact without touching the filters.
    if (mixTarget == 0.0f && mix_ == 0.0f)
    {
        for (auto& ch : lp_)
            for (auto& f : ch)
                f.reset();
        for (auto& ch : hp_)
            for (auto& f : ch)
                f.reset();
        tick_ += numSamples;
        while (tick_ >= hop_)
        {
            tick_ -= hop_;
            snap_.enabled = false;
            snap_.reductionDb = 0.0f;
            ++snap_.sequence;
            telemetry_.write(snap_);
        }
        return;
    }

    std::array<float, kMaxChannels> lo {}, hi {};
    for (int i = 0; i < numSamples; ++i)
    {
        float bodyAbs = 0.0f, highAbs = 0.0f;
        for (int ch = 0; ch < nCh; ++ch)
        {
            float x = channels[ch][i];
            if (!std::isfinite(x))
                x = 0.0f;
            auto& l = lp_[static_cast<size_t>(ch)];
            auto& h = hp_[static_cast<size_t>(ch)];
            lo[static_cast<size_t>(ch)] = l[1].process(l[0].process(x));
            hi[static_cast<size_t>(ch)] = h[1].process(h[0].process(x));
            bodyAbs = std::max(bodyAbs, std::abs(lo[static_cast<size_t>(ch)]));
            highAbs = std::max(highAbs, std::abs(hi[static_cast<size_t>(ch)]));
        }

        // Envelopes
        bodyEnv_ = std::max(bodyAbs, bodyEnv_ * envRelease_);
        highEnv_ = std::max(highAbs, highEnv_ * envRelease_);
        hitLevel_ = std::max(bodyEnv_, hitLevel_ * hitLevelCoef_);
        const float slowBefore = slowBody_;
        slowBody_ = bodyEnv_ + (slowBody_ - bodyEnv_) * slowCoef_;

        // Hit learning: onset -> measure the next few ms -> update k.
        if (refractory_ > 0)
            --refractory_;
        if (windowLeft_ > 0)
        {
            winBodyMax_ = std::max(winBodyMax_, bodyEnv_);
            winHighMax_ = std::max(winHighMax_, highEnv_);
            // Only real hits teach the ratio: within 12 dB of the recent loudest hit.
            // (Bleed leaking into the body band, e.g. from a crash, never qualifies.)
            if (--windowLeft_ == 0 && winBodyMax_ > kHitFloor && winBodyMax_ >= kHitRelative * hitLevel_)
            {
                // Remove the steady bleed already present (e.g. a ringing crash)
                // before measuring the drum's own brightness.
                const float own = std::sqrt(std::max(winHighMax_ * winHighMax_ - bleed_ * bleed_,
                                                     0.01f * winHighMax_ * winHighMax_));
                const float r = std::log10(std::max(own, 1.0e-9f) / winBodyMax_);
                // Down fast; up slowly, and never from a hit far brighter than
                // the drum has been (a cymbal landing on the hit).
                if (r <= logK_ + kOutlierLog)
                {
                    const float rate = r < logK_ ? kLearnDown : kLearnUp;
                    logK_ = std::clamp(logK_ + rate * (r - logK_), kMinLogK, kMaxLogK);
                    kLin_ = std::pow(10.0f, logK_);
                }
                ++hits_;
            }
        }
        else if (refractory_ == 0 && bodyEnv_ > kHitFloor && bodyEnv_ >= kOnsetRatio * std::max(slowBefore, 1.0e-6f))
        {
            windowLeft_ = windowSamples_;
            refractory_ = refractorySamples_;
            winBodyMax_ = bodyEnv_;
            winHighMax_ = highEnv_;
        }

        // Allowed high end: follows the body, may ring for `length`.
        allowed_ = std::max(kLin_ * bodyEnv_, allowed_ * lengthCoef_);

        // Bleed estimate: the high-band level the drum does NOT explain. It
        // only follows the high band while the drum accounts for less than
        // half of it, so it holds its value through a hit.
        if (allowed_ * tolerance < 0.5f * highEnv_)
            bleed_ = highEnv_ + (bleed_ - highEnv_) * bleedCoef_;

        // Gain on the high band = the stricter of
        //  * ceiling: the high band may not exceed what the drum explains (+ tolerance);
        //  * share:   keep the drum's expected share of the high band, D / sqrt(D^2 + B^2).
        // The ceiling acts immediately on new bleed; the share removes known bleed
        // under the drum's own ringing tail.
        float targetDb = 0.0f;
        if (highEnv_ > 1.0e-7f)
        {
            const float d = allowed_ * tolerance;
            const float ceiling = d / highEnv_;
            const float share = d / std::sqrt(d * d + bleed_ * bleed_ + 1.0e-20f);
            const float ratio = std::min(ceiling, share);
            targetDb = ratio < 1.0f ? std::max(floorDb, gainToDb(std::max(ratio, 1.0e-8f))) : 0.0f;
        }
        const float coef = targetDb > gainDb_ ? openCoef_ : closeCoef_;
        gainDb_ = targetDb + (gainDb_ - targetDb) * coef;
        const float g = dbToGain(gainDb_);
        tickMaxRed_ = std::max(tickMaxRed_, -gainDb_ * mix_);

        if (mix_ != mixTarget)
            mix_ = mixTarget > mix_ ? std::min(mixTarget, mix_ + mixStep_) : std::max(mixTarget, mix_ - mixStep_);

        for (int ch = 0; ch < nCh; ++ch)
        {
            const float x = std::isfinite(channels[ch][i]) ? channels[ch][i] : 0.0f;
            const float y = lo[static_cast<size_t>(ch)] + g * hi[static_cast<size_t>(ch)];
            channels[ch][i] = mix_ >= 1.0f ? y : x + mix_ * (y - x);
        }

        tickBody_ = std::max(tickBody_, bodyEnv_);
        tickHigh_ = std::max(tickHigh_, highEnv_);
        if (++tick_ >= hop_)
        {
            tick_ = 0;
            snap_.enabled = mixTarget > 0.0f;
            snap_.reductionDb = tickMaxRed_;
            snap_.bodyDb = gainToDb(tickBody_);
            snap_.highDb = gainToDb(tickHigh_);
            snap_.learnedRatioDb = logK_ * 20.0f;
            snap_.hitsSeen = hits_;
            ++snap_.sequence;
            telemetry_.write(snap_);
            tickMaxRed_ = tickBody_ = tickHigh_ = 0.0f;
        }
    }
}

} // namespace mixcore::drum
