#include "mixcore/comp/Compressor.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>

namespace mixcore::comp {

namespace {
constexpr double kRmsMs = 10.0;
constexpr double kSmoothMs = 20.0;
constexpr double kMixMs = 10.0;
constexpr double kAutoMakeupSeconds = 3.0;
constexpr double kSlowAttackMs = 300.0; // auto release: slow branch builds up only on sustained compression
constexpr double kMinSlowReleaseMs = 600.0;

double coefFor(double ms, double sr) { return ms <= 0.0 ? 0.0 : std::exp(-1.0 / (ms * 0.001 * sr)); }
} // namespace

bool Compressor::prepare(double sampleRate, int, int numChannels)
{
    prepared_ = false;
    if (!(sampleRate >= 8000.0 && sampleRate <= 768000.0) || numChannels < 1)
        return false;
    sampleRate_ = sampleRate;
    numChannels_ = std::min(numChannels, kMaxChannels);
    rmsCoef_ = coefFor(kRmsMs, sampleRate_);
    smoothCoef_ = coefFor(kSmoothMs, sampleRate_);
    avgCoef_ = coefFor(kAutoMakeupSeconds * 1000.0, sampleRate_);
    mixStep_ = static_cast<float>(1.0 / (kMixMs * 0.001 * sampleRate_));
    hop_ = msToSamples(10.0, sampleRate_);
    snap_.sampleRate = sampleRate_;
    prepared_ = true;
    hpfDesignedHz_ = -1.0f;
    updateCoefficients();
    reset();
    return true;
}

void Compressor::reset() noexcept
{
    for (auto& f : scHpf_)
        f.reset();
    ms_ = 0.0;
    gr_ = grSlow_ = 0.0;
    avgGr_ = 0.0;
    makeup_ = settings_.autoMakeup ? 0.0 : static_cast<double>(settings_.makeupDb);
    mix_ = static_cast<double>(settings_.mixPercent) * 0.01;
    moduleMix_ = (settings_.enabled && !bypassed_) ? 1.0f : 0.0f;
    hopCount_ = 0;
    hopInSq_ = hopOutSq_ = 0.0;
    hopInPeak_ = hopMaxGr_ = 0.0f;
    hopMaxLevel_ = kSilenceDb;
}

void Compressor::setSettings(const CompSettings& s) noexcept
{
    auto v = s;
    v.validate();
    const bool timing = v.attackMs != settings_.attackMs || v.releaseMs != settings_.releaseMs;
    const bool hpf = v.scHpfHz != settings_.scHpfHz;
    settings_ = v;
    if (prepared_ && (timing || hpf || hpfDesignedHz_ < 0.0f))
        updateCoefficients();
}

void Compressor::updateCoefficients() noexcept
{
    attCoef_ = coefFor(static_cast<double>(settings_.attackMs), sampleRate_);
    relCoef_ = coefFor(static_cast<double>(settings_.releaseMs), sampleRate_);
    slowAttCoef_ = coefFor(kSlowAttackMs, sampleRate_);
    slowRelCoef_ = coefFor(std::max(kMinSlowReleaseMs, 5.0 * static_cast<double>(settings_.releaseMs)), sampleRate_);
    if (settings_.scHpfHz != hpfDesignedHz_)
    {
        const double f = std::min(static_cast<double>(settings_.scHpfHz), 0.45 * sampleRate_);
        for (auto& b : scHpf_)
            b.setHighPass(sampleRate_, f, 0.70710678);
        hpfDesignedHz_ = settings_.scHpfHz;
    }
}

void Compressor::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0)
        return;
    const int nCh = std::min(numChannels, numChannels_);
    const bool want = settings_.enabled && !bypassed_;
    const float target = want ? 1.0f : 0.0f;
    const auto& s = settings_;
    const bool rms = s.detector == Detector::Rms;
    const float invCh = nCh > 0 ? 1.0f / static_cast<float>(nCh) : 1.0f;
    const double mixTarget = static_cast<double>(s.mixPercent) * 0.01;

    for (int i = 0; i < numSamples; ++i)
    {
        // Input analysis (always, for the Coach and the meters).
        float inSq = 0.0f, inPk = 0.0f, scSq = 0.0f;
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float x = channels[ch][i];
            inSq += x * x;
            inPk = std::max(inPk, std::abs(x));
            if (moduleMix_ > 0.0f || want)
            {
                const float sc = s.scHpfOn ? scHpf_[static_cast<size_t>(ch)].process(x) : x;
                scSq = std::max(scSq, sc * sc);
            }
        }
        hopInSq_ += static_cast<double>(inSq * invCh);
        hopInPeak_ = std::max(hopInPeak_, inPk);

        float g = 1.0f;
        if (moduleMix_ > 0.0f || want)
        {
            // Detector
            float levelDb;
            if (rms)
            {
                ms_ = rmsCoef_ * ms_ + (1.0 - rmsCoef_) * static_cast<double>(scSq);
                levelDb = powerToDb(ms_);
            }
            else
            {
                levelDb = gainToDb(std::sqrt(scSq));
            }
            const auto tgt = static_cast<double>(gainChangeDb(levelDb, s.thresholdDb, s.ratio, s.kneeDb));
            // Smoothing in dB: attack when reduction grows, release when it shrinks.
            gr_ = tgt < gr_ ? attCoef_ * gr_ + (1.0 - attCoef_) * tgt : relCoef_ * gr_ + (1.0 - relCoef_) * tgt;
            double grUsed = gr_;
            if (s.autoRelease)
            {
                grSlow_ = tgt < grSlow_ ? slowAttCoef_ * grSlow_ + (1.0 - slowAttCoef_) * tgt
                                        : slowRelCoef_ * grSlow_ + (1.0 - slowRelCoef_) * tgt;
                grUsed = std::min(gr_, grSlow_);
            }
            else
            {
                grSlow_ = gr_;
            }

            // Makeup: manual (smoothed) or auto (slow average of the reduction while the source is active).
            if (levelDb > s.thresholdDb - 10.0f)
                avgGr_ = avgCoef_ * avgGr_ + (1.0 - avgCoef_) * grUsed;
            const double makeTarget = s.autoMakeup ? std::clamp(-avgGr_, 0.0, 18.0) : static_cast<double>(s.makeupDb);
            makeup_ = smoothCoef_ * makeup_ + (1.0 - smoothCoef_) * makeTarget;
            mix_ = smoothCoef_ * mix_ + (1.0 - smoothCoef_) * mixTarget;

            if (moduleMix_ != target)
                moduleMix_ = target > moduleMix_ ? std::min(target, moduleMix_ + mixStep_) : std::max(target, moduleMix_ - mixStep_);

            const auto wet = static_cast<float>(dbToGainD(grUsed + makeup_));
            const auto m = static_cast<float>(mix_) * moduleMix_;
            g = 1.0f + m * (wet - 1.0f);
            for (int ch = 0; ch < nCh; ++ch)
                channels[ch][i] *= g;

            hopMaxGr_ = std::max(hopMaxGr_, static_cast<float>(-grUsed) * moduleMix_);
            hopMaxLevel_ = std::max(hopMaxLevel_, levelDb);
            if (moduleMix_ <= 0.0f && !want)
            {
                // Fully off: forget the envelope so switching on starts clean.
                ms_ = 0.0;
                gr_ = grSlow_ = 0.0;
                for (auto& f : scHpf_)
                    f.reset();
            }
        }
        hopOutSq_ += static_cast<double>(inSq * invCh * g * g);

        if (++hopCount_ >= hop_)
        {
            const double n = static_cast<double>(hopCount_);
            const float inRmsDb = powerToDb(hopInSq_ / n);
            tap_.push(inRmsDb, gainToDb(hopInPeak_));
            snap_.sequence += 1;
            snap_.enabled = want;
            snap_.gainReductionDb = hopMaxGr_;
            snap_.detectorDb = (moduleMix_ > 0.0f || want) ? hopMaxLevel_ : (rms ? inRmsDb : gainToDb(hopInPeak_));
            snap_.makeupDb = static_cast<float>(makeup_);
            snap_.outputRmsDb = powerToDb(hopOutSq_ / n);
            telemetry_.write(snap_);
            hopCount_ = 0;
            hopInSq_ = hopOutSq_ = 0.0;
            hopInPeak_ = hopMaxGr_ = 0.0f;
            hopMaxLevel_ = kSilenceDb;
        }
    }
}

} // namespace mixcore::comp
