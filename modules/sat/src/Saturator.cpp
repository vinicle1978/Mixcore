#include "mixcore/sat/Saturator.h"

#include "mixcore/core/DbMath.h"
#include "mixcore/sat/SatCurves.h"

#include <algorithm>
#include <cmath>

namespace mixcore::sat {

namespace {
constexpr double kSmoothMs = 20.0;
constexpr double kMixMs = 10.0;
constexpr double kPowerMs = 300.0;
constexpr double kAgcMs = 80.0;
constexpr double kDcHz = 5.0;
constexpr float kGateDb = -60.0f; // auto gain only learns while the signal is above this
double coefFor(double ms, double sr) { return std::exp(-1.0 / (ms * 0.001 * sr)); }
} // namespace

bool Saturator::prepare(double sampleRate, int, int numChannels)
{
    prepared_ = false;
    if (!(sampleRate >= 8000.0 && sampleRate <= 768000.0) || numChannels < 1)
        return false;
    sampleRate_ = sampleRate;
    numChannels_ = std::min(numChannels, kMaxChannels);
    smooth_ = coefFor(kSmoothMs, sampleRate_);
    mixStep_ = static_cast<float>(1.0 / (kMixMs * 0.001 * sampleRate_));
    powCoef_ = coefFor(kPowerMs, sampleRate_);
    agcCoef_ = coefFor(kAgcMs, sampleRate_);
    dcR_ = std::exp(-6.283185307179586 * kDcHz / sampleRate_);
    dcR4_ = std::exp(-6.283185307179586 * kDcHz / (sampleRate_ * Oversampler4x::kFactor));
    hop_ = msToSamples(10.0, sampleRate_);
    snap_.sampleRate = sampleRate_;
    prepared_ = true;
    reset();
    return true;
}

void Saturator::reset() noexcept
{
    for (auto& o : os_)
        o.reset();
    for (auto& d : dryDelay_)
        d.fill(0.0f);
    delayPos_ = 0;
    dcX_.fill(0.0);
    dcY_.fill(0.0);
    drive_ = static_cast<double>(dbToGain(settings_.driveDb));
    mix_ = static_cast<double>(settings_.mixPercent) * 0.01;
    out_ = static_cast<double>(dbToGain(settings_.outputDb));
    agc_ = 1.0;
    pDry_ = pWet_ = 0.0;
    learned_ = 0;
    moduleMix_ = (settings_.enabled && !bypassed_) ? 1.0f : 0.0f;
    studio_ = settings_.quality == SatQuality::Studio;
    hopCount_ = 0;
    hopInSq_ = hopLin_ = hopDist_ = 0.0;
    hopInPeak_ = hopOutPeak_ = 0.0f;
}

void Saturator::setSettings(const SatSettings& s) noexcept
{
    settings_ = s;
    settings_.validate();
}

int Saturator::latencySamples() const noexcept
{
    return (settings_.enabled && !bypassed_ && settings_.quality == SatQuality::Studio) ? kDelay : 0;
}

float Saturator::wetSample(int ch, float x) noexcept
{
    // x is at the running rate (1x or 4x). Returns dry + mix * (wet - dry), before output gain.
    const auto d = static_cast<float>(drive_);
    const float driven = x * d;
    const float shaped = shape(settings_.type, driven);
    // DC blocker on the saturated signal (the Tube curve is asymmetric).
    auto& x1 = dcX_[static_cast<size_t>(ch)];
    auto& y1 = dcY_[static_cast<size_t>(ch)];
    const double r = studio_ ? dcR4_ : dcR_;
    const double y = static_cast<double>(shaped) - x1 + r * y1;
    x1 = static_cast<double>(shaped);
    y1 = y;
    const auto wet = static_cast<float>(y);
    accDry_ += static_cast<double>(x * x);
    accWet_ += y * y;
    accLin_ += static_cast<double>(driven * driven);
    accDist_ += static_cast<double>((shaped - driven) * (shaped - driven));
    const auto g = static_cast<float>(agc_);
    return x + static_cast<float>(mix_) * (g * wet - x);
}

void Saturator::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    if (!prepared_ || numSamples <= 0)
        return;
    const int nCh = std::min(numChannels, numChannels_);
    const bool want = settings_.enabled && !bypassed_;
    const float target = want ? 1.0f : 0.0f;
    const bool wantStudio = settings_.quality == SatQuality::Studio;
    const auto driveT = static_cast<double>(dbToGain(settings_.driveDb));
    const double mixT = static_cast<double>(settings_.mixPercent) * 0.01;
    const auto outT = static_cast<double>(dbToGain(settings_.outputDb));
    const float invCh = 1.0f / static_cast<float>(std::max(1, nCh));

    for (int i = 0; i < numSamples; ++i)
    {
        float inSq = 0.0f, inPk = 0.0f;
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float x = channels[ch][i];
            inputs_[static_cast<size_t>(ch)] = x;
            inSq += x * x;
            inPk = std::max(inPk, std::abs(x));
        }
        hopInSq_ += static_cast<double>(inSq * invCh);
        hopInPeak_ = std::max(hopInPeak_, inPk);

        if (moduleMix_ > 0.0f || want)
        {
            // Starting from fully off, or quality changed and now faded out: start the filters clean.
            if (moduleMix_ <= 0.0f && (studio_ != wantStudio || want))
            {
                studio_ = wantStudio;
                for (auto& o : os_)
                    o.reset();
                dcX_.fill(0.0);
                dcY_.fill(0.0);
            }
            drive_ = smooth_ * drive_ + (1.0 - smooth_) * driveT;
            mix_ = smooth_ * mix_ + (1.0 - smooth_) * mixT;
            out_ = smooth_ * out_ + (1.0 - smooth_) * outT;
            if (moduleMix_ != target)
                moduleMix_ = target > moduleMix_ ? std::min(target, moduleMix_ + mixStep_) : std::max(target, moduleMix_ - mixStep_);
            const bool fading = studio_ != wantStudio; // quality changed while on: fade out, switch, fade in
            if (fading)
                moduleMix_ = std::max(0.0f, moduleMix_ - 2.0f * mixStep_);

            accDry_ = accWet_ = accLin_ = accDist_ = 0.0;
            const auto o = static_cast<float>(out_);
            for (int ch = 0; ch < nCh; ++ch)
            {
                const float x = channels[ch][i];
                float processed, dry;
                if (studio_)
                {
                    float up[Oversampler4x::kFactor];
                    os_[static_cast<size_t>(ch)].up(x, up);
                    for (auto& v : up)
                        v = wetSample(ch, v);
                    processed = os_[static_cast<size_t>(ch)].down(up) * o;
                    dry = dryDelay_[static_cast<size_t>(ch)][static_cast<size_t>(delayPos_)];
                }
                else
                {
                    processed = wetSample(ch, x) * o;
                    dry = x;
                }
                const float y = dry + moduleMix_ * (processed - dry);
                channels[ch][i] = y;
                hopOutPeak_ = std::max(hopOutPeak_, std::abs(y));
            }

            // Auto gain: match the saturated signal's loudness to the dry signal (slow, only while there is signal).
            const double n = studio_ ? static_cast<double>(nCh * Oversampler4x::kFactor) : static_cast<double>(nCh);
            const double dryP = accDry_ / n, wetP = accWet_ / n;
            // Just after switching on, the trackers start as running averages so the level is matched
            // within a few milliseconds (no loudness bump while the slow averages fill up).
            const double warm = 1.0 - 1.0 / static_cast<double>(++learned_ + 1);
            if (powerToDb(dryP) > kGateDb)
            {
                const double pc = std::min(powCoef_, warm);
                pDry_ = pc * pDry_ + (1.0 - pc) * dryP;
                pWet_ = pc * pWet_ + (1.0 - pc) * wetP;
            }
            else
                --learned_;
            double agcT = 1.0;
            if (settings_.autoGain && pWet_ > 1e-12 && pDry_ > 1e-12)
                agcT = std::clamp(std::sqrt(pDry_ / pWet_), 0.063, 4.0); // -24 .. +12 dB
            const double ac = std::min(agcCoef_, warm);
            agc_ = ac * agc_ + (1.0 - ac) * agcT;
            hopLin_ += accLin_;
            hopDist_ += accDist_;

            if (moduleMix_ <= 0.0f && !want)
            {
                learned_ = 0;
                pDry_ = pWet_ = 0.0;
                agc_ = 1.0;
                dcX_.fill(0.0);
                dcY_.fill(0.0);
            }
        }
        else
        {
            hopOutPeak_ = std::max(hopOutPeak_, inPk);
        }

        // The dry delay line always runs, so switching Studio on is a clean time shift (the host compensates it).
        for (int ch = 0; ch < nCh; ++ch)
            dryDelay_[static_cast<size_t>(ch)][static_cast<size_t>(delayPos_)] = inputs_[static_cast<size_t>(ch)];
        delayPos_ = (delayPos_ + 1) % kDelay;

        if (++hopCount_ >= hop_)
        {
            const double cnt = static_cast<double>(hopCount_);
            tap_.push(powerToDb(hopInSq_ / cnt), gainToDb(hopInPeak_));
            snap_.sequence += 1;
            snap_.enabled = want;
            snap_.inputPeakDb = gainToDb(hopInPeak_);
            snap_.saturationDb = (want && hopLin_ > 1e-12) ? powerToDb(hopDist_ / hopLin_) : kSilenceDb;
            snap_.autoGainDb = gainToDb(static_cast<float>(agc_));
            snap_.outputPeakDb = gainToDb(hopOutPeak_);
            telemetry_.write(snap_);
            hopCount_ = 0;
            hopInSq_ = hopLin_ = hopDist_ = 0.0;
            hopInPeak_ = hopOutPeak_ = 0.0f;
        }
    }
}

} // namespace mixcore::sat
