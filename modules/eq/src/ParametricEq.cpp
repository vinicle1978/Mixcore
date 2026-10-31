#include "mixcore/eq/ParametricEq.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>

namespace mixcore::eq {

namespace {
constexpr int kControlInterval = 32;
constexpr double kSmoothMs = 20.0;
constexpr double kMixMs = 10.0;
} // namespace

bool ParametricEq::prepare(double sampleRate, int, int numChannels)
{
    prepared_ = false;
    if (!(sampleRate >= 8000.0 && sampleRate <= 768000.0) || numChannels < 1)
        return false;
    sampleRate_ = sampleRate;
    numChannels_ = std::min(numChannels, kMaxChannels);
    smoothCoef_ = std::exp(-static_cast<double>(kControlInterval) / (kSmoothMs * 0.001 * sampleRate_));
    mixStep_ = static_cast<float>(1.0 / (kMixMs * 0.001 * sampleRate_));
    outGainCoef_ = timeConstantCoef(kSmoothMs * 0.001, sampleRate_);
    hop_ = msToSamples(10.0, sampleRate_);
    prepared_ = true;
    reset();
    return true;
}

void ParametricEq::reset() noexcept
{
    if (!prepared_)
        return;
    for (int i = 0; i < kNumBands; ++i)
    {
        auto& rt = bands_[static_cast<size_t>(i)];
        const auto& b = settings_.bands[static_cast<size_t>(i)];
        rt.logF = std::log(static_cast<double>(b.freqHz));
        rt.gainDb = static_cast<double>(b.gainDb);
        rt.logQ = std::log(static_cast<double>(b.q));
        rt.mix = b.on ? 1.0f : 0.0f;
        for (auto& ch : rt.state)
            for (auto& st : ch)
                st = {};
        rt.designed = false;
    }
    updateCoefficients(true);
    moduleMix_ = (settings_.enabled && !bypassed_) ? 1.0f : 0.0f;
    outGain_ = dbToGain(settings_.outputDb);
    controlCounter_ = 0;
    tick_ = 0;
}

void ParametricEq::setSettings(const EqSettings& s) noexcept
{
    const bool slopeChanged = [&] {
        for (int i = 0; i < kNumBands; ++i)
            if (s.bands[static_cast<size_t>(i)].slope != settings_.bands[static_cast<size_t>(i)].slope)
                return true;
        return false;
    }();
    settings_ = s;
    settings_.validate();
    if (prepared_ && slopeChanged)
        updateCoefficients(false); // section count changes immediately (states of new sections start at 0)
}

void ParametricEq::updateCoefficients(bool snap) noexcept
{
    for (int i = 0; i < kNumBands; ++i)
    {
        auto& rt = bands_[static_cast<size_t>(i)];
        const auto& b = settings_.bands[static_cast<size_t>(i)];
        const double tf = std::log(static_cast<double>(b.freqHz)), tg = static_cast<double>(b.gainDb),
                     tq = std::log(static_cast<double>(b.q));
        bool changed = !rt.designed;
        if (snap)
        {
            changed = changed || rt.logF != tf || rt.gainDb != tg || rt.logQ != tq;
            rt.logF = tf;
            rt.gainDb = tg;
            rt.logQ = tq;
        }
        else
        {
            const auto glide = [&](double& v, double target, double eps) {
                if (std::abs(target - v) > eps)
                {
                    v = target + (v - target) * smoothCoef_;
                    changed = true;
                }
                else if (v != target)
                {
                    v = target;
                    changed = true;
                }
            };
            glide(rt.logF, tf, 1.0e-4);
            glide(rt.gainDb, tg, 1.0e-3);
            glide(rt.logQ, tq, 1.0e-4);
        }
        const auto type = EqSettings::type(i);
        const int oldCount = rt.design.count;
        const bool pass = type == BandType::HighPass || type == BandType::LowPass;
        const int newCount = pass ? (b.slope < 18.0f ? 1 : (b.slope < 36.0f ? 2 : 4)) : 1;
        if (changed || newCount != oldCount) // design only when something moved (keeps the audio thread light)
        {
            rt.design = designBand(type, sampleRate_, std::exp(rt.logF), std::exp(rt.logQ), rt.gainDb, static_cast<double>(b.slope));
            if (rt.design.count != oldCount)
                for (auto& ch : rt.state)
                    for (int k = oldCount; k < kMaxSections; ++k)
                        ch[static_cast<size_t>(k)] = {};
        }
        rt.designed = true;
    }
}

void ParametricEq::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    if (!prepared_ || channels == nullptr || numSamples <= 0)
        return;
    const int nCh = std::min(numChannels, numChannels_);
    if (nCh <= 0)
        return;

    const bool active = settings_.enabled && !bypassed_;
    const float moduleTarget = active ? 1.0f : 0.0f;
    const float outTarget = dbToGain(settings_.outputDb);
    const float invCh = 1.0f / static_cast<float>(nCh);

    // Off and settled: untouched (the analyzer still sees the signal).
    if (!active && moduleMix_ == 0.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float m = 0.0f;
            for (int ch = 0; ch < nCh; ++ch)
                m += std::isfinite(channels[ch][i]) ? channels[ch][i] : 0.0f;
            inTap_.push(m * invCh);
            outTap_.push(m * invCh);
        }
        tick_ += numSamples;
        while (tick_ >= hop_)
        {
            tick_ -= hop_;
            snap_.enabled = false;
            snap_.activeBands = settings_.activeBandCount();
            snap_.sampleRate = sampleRate_;
            ++snap_.sequence;
            telemetry_.write(snap_);
        }
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (controlCounter_-- <= 0)
        {
            controlCounter_ = kControlInterval - 1;
            updateCoefficients(false);
        }
        if (moduleMix_ != moduleTarget)
            moduleMix_ = moduleTarget > moduleMix_ ? std::min(1.0f, moduleMix_ + mixStep_) : std::max(0.0f, moduleMix_ - mixStep_);
        outGain_ = outTarget + (outGain_ - outTarget) * outGainCoef_;
        for (int b = 0; b < kNumBands; ++b)
        {
            auto& rt = bands_[static_cast<size_t>(b)];
            const float t = settings_.bands[static_cast<size_t>(b)].on ? 1.0f : 0.0f;
            if (rt.mix != t)
                rt.mix = t > rt.mix ? std::min(1.0f, rt.mix + mixStep_) : std::max(0.0f, rt.mix - mixStep_);
        }

        float inMono = 0.0f, outMono = 0.0f;
        for (int ch = 0; ch < nCh; ++ch)
        {
            float x = channels[ch][i];
            if (!std::isfinite(x))
                x = 0.0f;
            inMono += x;
            double y = static_cast<double>(x);
            for (int b = 0; b < kNumBands; ++b)
            {
                auto& rt = bands_[static_cast<size_t>(b)];
                if (rt.mix <= 0.0f)
                {
                    // Band off: keep its filter state quiet so turning it on starts clean.
                    continue;
                }
                double v = y;
                auto& st = rt.state[static_cast<size_t>(ch)];
                for (int k = 0; k < rt.design.count; ++k)
                {
                    const auto& c = rt.design.sections[static_cast<size_t>(k)];
                    auto& s = st[static_cast<size_t>(k)];
                    const double out = c.b0 * v + s.z1;
                    s.z1 = c.b1 * v - c.a1 * out + s.z2;
                    s.z2 = c.b2 * v - c.a2 * out;
                    v = out;
                }
                y = rt.mix >= 1.0f ? v : y + static_cast<double>(rt.mix) * (v - y);
            }
            const float wet = static_cast<float>(y) * outGain_;
            const float out = moduleMix_ >= 1.0f ? wet : x + moduleMix_ * (wet - x);
            channels[ch][i] = out;
            outMono += out;
        }
        inTap_.push(inMono * invCh);
        outTap_.push(outMono * invCh);

        if (++tick_ >= hop_)
        {
            tick_ = 0;
            snap_.enabled = active;
            snap_.activeBands = settings_.activeBandCount();
            snap_.sampleRate = sampleRate_;
            ++snap_.sequence;
            telemetry_.write(snap_);
        }
    }

    // Fully faded out: clear filter state so the next switch-on starts clean.
    if (!active && moduleMix_ == 0.0f)
        for (auto& rt : bands_)
            for (auto& ch : rt.state)
                for (auto& st : ch)
                    st = {};
    for (auto& rt : bands_)
        if (rt.mix == 0.0f)
            for (auto& ch : rt.state)
                for (auto& st : ch)
                    st = {};
}

} // namespace mixcore::eq
