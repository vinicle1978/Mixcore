#include "mixcore/drum/DrumReplacer.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>

namespace mixcore::drum {

namespace {
constexpr double kEnvReleaseMs = 10.0;
constexpr double kSlowMs = 5.0;  // rise time of the floor envelope
constexpr double kFadeMs = 5.0;       // latency switch: fade out, switch, fade in
constexpr double kMixMs = 10.0;       // on/off crossfade
constexpr double kGainSmoothMs = 10.0;
constexpr double kChokeMs = 5.0;
constexpr float kRearmRatio = 0.63f;  // -4 dB below threshold re-arms...
constexpr double kRearmHoldMs = 15.0; // ...once it has stayed there this long (ignores the dips between
                                      // the cycles of a low, decaying drum tone)
constexpr float kJumpRatio = 2.0f;    // +6 dB sudden rise re-arms (fast rolls/flams)
constexpr int kChokeAfterVoices = 4;  // more overlapping hits than this: oldest fades out
constexpr double kButterQ1 = 0.5411961;
constexpr double kButterQ2 = 1.3065630;

inline float hermite(const std::vector<float>& s, double pos) noexcept
{
    const auto n = static_cast<std::ptrdiff_t>(s.size());
    const auto i = static_cast<std::ptrdiff_t>(pos);
    const float t = static_cast<float>(pos - static_cast<double>(i));
    const auto at = [&](std::ptrdiff_t k) { return s[static_cast<size_t>(std::clamp<std::ptrdiff_t>(k, 0, n - 1))]; };
    const float x0 = at(i - 1), x1 = at(i), x2 = at(i + 1), x3 = at(i + 2);
    const float c1 = 0.5f * (x2 - x0);
    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * t + c2) * t + c1) * t + x1;
}
} // namespace

bool DrumReplacer::prepare(double sampleRate, int, int numChannels)
{
    prepared_ = false;
    if (!(sampleRate >= 8000.0 && sampleRate <= 768000.0) || numChannels < 1)
        return false;
    sampleRate_ = sampleRate;
    numChannels_ = std::min(numChannels, kMaxChannels);
    envRelease_ = timeConstantCoef(kEnvReleaseMs * 0.001, sampleRate_);
    slowCoef_ = timeConstantCoef(kSlowMs * 0.001, sampleRate_);
    gainCoef_ = timeConstantCoef(kGainSmoothMs * 0.001, sampleRate_);
    fadeStep_ = static_cast<float>(1.0 / (kFadeMs * 0.001 * sampleRate_));
    mixStep_ = static_cast<float>(1.0 / (kMixMs * 0.001 * sampleRate_));
    hop_ = msToSamples(10.0, sampleRate_);
    rearmHold_ = msToSamples(kRearmHoldMs, sampleRate_);
    delayCapacity_ = msToSamples(kMaxLookaheadMs, sampleRate_) + 2;
    delay_.assign(static_cast<size_t>(delayCapacity_) * kMaxChannels, 0.0f);
    designedLow_ = designedHigh_ = -1.0f;
    prepared_ = true;
    design();
    reset();
    return true;
}

void DrumReplacer::reset() noexcept
{
    if (!prepared_)
        return;
    for (auto& ch : hpf_)
        for (auto& f : ch)
            f.reset();
    for (auto& ch : lpf_)
        for (auto& f : ch)
            f.reset();
    env_ = slowEnv_ = 0.0f;
    armed_ = true;
    belowCount_ = 0;
    sinceLast_ = 1 << 30;
    windowLeft_ = 0;
    windowPeak_ = 0.0f;
    std::fill(delay_.begin(), delay_.end(), 0.0f);
    delayPos_ = 0;
    for (auto& p : pending_)
        p.used = false;
    for (auto& v : voices_)
        v.sample = nullptr;
    roundRobin_.fill(0);
    const bool active = settings_.replaceActive() && !bypassed_;
    wantLatency_ = settings_.replaceActive() ? std::min(msToSamples(static_cast<double>(settings_.replaceLookaheadMs), sampleRate_), delayCapacity_ - 2) : 0;
    latency_.store(wantLatency_, std::memory_order_relaxed);
    fade_ = 1.0f;
    mix_ = active ? 1.0f : 0.0f;
    const float mix = settings_.replaceMix * 0.01f;
    originalGain_ = (settings_.replaceOriginalDb <= -59.9f ? 0.0f : dbToGain(settings_.replaceOriginalDb))
                    * std::min(1.0f, 2.0f * (1.0f - mix));
    sampleGain_ = dbToGain(settings_.replaceSampleDb) * std::min(1.0f, 2.0f * mix);
    tick_ = 0;
    tickDet_ = 0.0f;
    tickHits_ = 0;
    hitRing_.fill(0);
    hitRingPos_ = hitsLastSecond_ = 0;
    secondsSinceHit_ = 1.0e6f;
    snap_ = ReplaceTelemetry {};
}

void DrumReplacer::setSettings(const DrumSettings& s) noexcept
{
    settings_ = s;
    settings_.validate();
    if (prepared_)
        design();
}

void DrumReplacer::setKeyOffset(int samples) noexcept
{
    keyOffset_ = std::clamp(samples, 0, prepared_ ? msToSamples(kMaxKeyOffsetMs, sampleRate_) : 0);
}

void DrumReplacer::design() noexcept
{
    const float nyq = 0.45f * static_cast<float>(sampleRate_);
    const float low = settings_.replaceDetectLowHz;
    const float high = std::min(settings_.replaceDetectHighHz, nyq);
    useHpf_ = low > 25.0f;
    useLpf_ = high < std::min(19000.0f, nyq);
    if (std::abs(low - designedLow_) > 1.0e-3f)
    {
        designedLow_ = low;
        for (auto& ch : hpf_)
        {
            ch[0].setHighPass(sampleRate_, static_cast<double>(low), kButterQ1);
            ch[1].setHighPass(sampleRate_, static_cast<double>(low), kButterQ2);
        }
    }
    if (std::abs(high - designedHigh_) > 1.0e-3f)
    {
        designedHigh_ = high;
        for (auto& ch : lpf_)
        {
            ch[0].setLowPass(sampleRate_, static_cast<double>(high), kButterQ1);
            ch[1].setLowPass(sampleRate_, static_cast<double>(high), kButterQ2);
        }
    }
    attackSamples_ = settings_.replaceAttackMs > 0.0f ? msToSamples(static_cast<double>(settings_.replaceAttackMs), sampleRate_) : 0;
    lengthSamples_ = settings_.replaceLengthMs >= 1999.0f ? 0 : msToSamples(static_cast<double>(settings_.replaceLengthMs), sampleRate_);
    lengthFadeSamples_ = std::max(1, std::min(msToSamples(30.0, sampleRate_), lengthSamples_ / 3));
    pitchRatio_ = std::pow(2.0, static_cast<double>(settings_.replacePitchSt) / 12.0);
}

void DrumReplacer::startVoice(float velocity) noexcept
{
    if (kit_ == nullptr || kit_->empty())
        return;
    const int nLayers = std::min(static_cast<int>(kit_->layers.size()), kMaxLayers);
    const int layer = std::clamp(static_cast<int>(velocity * static_cast<float>(nLayers)), 0, nLayers - 1);
    const auto& rr = kit_->layers[static_cast<size_t>(layer)].roundRobin;
    if (rr.empty())
        return;
    const auto& sample = rr[roundRobin_[static_cast<size_t>(layer)]++ % rr.size()];

    // Too many overlapping hits: let the oldest ring out quickly (avoids build-up).
    int active = 0;
    Voice* oldest = nullptr;
    for (auto& v : voices_)
        if (v.sample != nullptr && v.fadeOutStep == 0.0f)
        {
            ++active;
            if (oldest == nullptr || v.order < oldest->order)
                oldest = &v;
        }
    if (active >= kChokeAfterVoices && oldest != nullptr)
        oldest->fadeOutStep = static_cast<float>(1.0 / (kChokeMs * 0.001 * sampleRate_));

    Voice* slot = nullptr;
    for (auto& v : voices_)
        if (v.sample == nullptr)
        {
            slot = &v;
            break;
        }
    if (slot == nullptr) // all busy: steal the oldest
    {
        slot = &voices_[0];
        for (auto& v : voices_)
            if (v.order < slot->order)
                slot = &v;
    }
    slot->sample = &sample;
    slot->pos = 0.0;
    slot->rate = pitchRatio_ * sample.sampleRate / sampleRate_;
    slot->gain = std::sqrt(std::max(velocity, 0.05f)); // layers carry the rest of the dynamics
    slot->age = 0;
    slot->fadeOut = 1.0f;
    slot->fadeOutStep = 0.0f;
    slot->order = ++voiceOrder_;
}

void DrumReplacer::renderVoices(float* outL, float* outR, int) noexcept
{
    float l = 0.0f, r = 0.0f;
    for (auto& v : voices_)
    {
        if (v.sample == nullptr)
            continue;
        const auto& s = *v.sample;
        if (v.pos >= static_cast<double>(s.frames() - 1) || (lengthSamples_ > 0 && v.age >= lengthSamples_))
        {
            v.sample = nullptr;
            continue;
        }
        float env = v.gain * v.fadeOut;
        if (attackSamples_ > 0 && v.age < attackSamples_)
            env *= static_cast<float>(v.age) / static_cast<float>(attackSamples_);
        if (lengthSamples_ > 0)
        {
            const int left = lengthSamples_ - v.age;
            if (left < lengthFadeSamples_)
                env *= 0.5f - 0.5f * std::cos(3.14159265f * static_cast<float>(left) / static_cast<float>(lengthFadeSamples_));
        }
        const float a = hermite(s.left, v.pos);
        const float b = s.stereo() ? hermite(s.right, v.pos) : a;
        l += a * env;
        r += b * env;
        v.pos += v.rate;
        ++v.age;
        if (v.fadeOutStep > 0.0f)
        {
            v.fadeOut -= v.fadeOutStep;
            if (v.fadeOut <= 0.0f)
                v.sample = nullptr;
        }
    }
    *outL = l;
    *outR = r;
}

void DrumReplacer::process(float* const* audio, const float* const* key, int numChannels, int numSamples) noexcept
{
    if (!prepared_ || audio == nullptr || numSamples <= 0)
        return;
    const int nCh = std::min(numChannels, numChannels_);
    if (nCh <= 0)
        return;
    if (key == nullptr)
        key = audio;

    // Kit hand-over: a new kit stops the voices of the old one.
    if (const auto* k = kits_.acquire(); k != kit_)
    {
        for (auto& v : voices_)
            v.sample = nullptr;
        roundRobin_.fill(0);
        kit_ = k;
    }

    const bool active = settings_.replaceActive() && !bypassed_;
    const bool keepLatency = settings_.replaceActive() && bypassed_;
    const int lookahead = std::min(msToSamples(static_cast<double>(settings_.replaceLookaheadMs), sampleRate_), delayCapacity_ - 2);
    const float thr = dbToGain(settings_.replaceThresholdDb);
    const float rearm = thr * kRearmRatio;
    const int retrigger = msToSamples(static_cast<double>(settings_.replaceRetriggerMs), sampleRate_);
    const float dynamics = settings_.replaceDynamics * 0.01f;
    // Mix: original fades out over the upper half, samples fade in over the lower half;
    // at 50 % both play at full level (augmentation).
    const float mix = settings_.replaceMix * 0.01f;
    const float targetOriginal = (settings_.replaceOriginalDb <= -59.9f ? 0.0f : dbToGain(settings_.replaceOriginalDb))
                                 * std::min(1.0f, 2.0f * (1.0f - mix));
    const float targetSample = dbToGain(settings_.replaceSampleDb) * std::min(1.0f, 2.0f * mix);

    // Off and settled: untouched audio, zero latency.
    if (!active && !keepLatency && mix_ == 0.0f && latency_.load(std::memory_order_relaxed) == 0 && fade_ >= 1.0f)
    {
        for (auto& v : voices_)
            v.sample = nullptr;
        for (auto& p : pending_)
            p.used = false;
        windowLeft_ = 0;
        env_ = slowEnv_ = 0.0f;
        armed_ = true;
        tick_ += numSamples;
        while (tick_ >= hop_)
        {
            tick_ -= hop_;
            snap_.enabled = false;
            snap_.kitLoaded = kit_ != nullptr && !kit_->empty();
            snap_.detectorDb = -150.0f;
            snap_.hitsThisTick = 0;
            snap_.activeVoices = 0;
            snap_.latencySamples = 0;
            snap_.thresholdDb = settings_.replaceThresholdDb;
            ++snap_.sequence;
            telemetry_.write(snap_);
        }
        kits_.blockDone();
        return;
    }

    int latency = latency_.load(std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i)
    {
        // ---- Latency / on-off state --------------------------------------------
        // Host bypass keeps the latency (the original is passed delayed), so
        // bypassing never changes the plugin's reported latency.
        wantLatency_ = (active || keepLatency) ? lookahead : (mix_ > 0.0f ? latency : 0);
        if (wantLatency_ != latency)
        {
            fade_ -= fadeStep_;
            if (fade_ <= 0.0f)
            {
                fade_ = 0.0f;
                latency = wantLatency_;
                latency_.store(latency, std::memory_order_relaxed);
                for (auto& p : pending_)
                    p.used = false;
                windowLeft_ = 0;
            }
        }
        else if (fade_ < 1.0f)
        {
            fade_ = std::min(1.0f, fade_ + fadeStep_);
        }
        const float mixTarget = (active && latency == lookahead) ? 1.0f : 0.0f;
        if (mix_ != mixTarget)
            mix_ = mixTarget > mix_ ? std::min(mixTarget, mix_ + mixStep_) : std::max(mixTarget, mix_ - mixStep_);
        originalGain_ = targetOriginal + (originalGain_ - targetOriginal) * gainCoef_;
        sampleGain_ = targetSample + (sampleGain_ - targetSample) * gainCoef_;

        // ---- Scheduled hits (start exactly at the hit in the delayed original) --
        for (auto& p : pending_)
            if (p.used && --p.countdown <= 0)
            {
                p.used = false;
                startVoice(p.velocity);
            }

        // ---- Detector ----------------------------------------------------------
        float a = 0.0f;
        for (int ch = 0; ch < nCh; ++ch)
        {
            float s = key[ch][i];
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
        env_ = std::max(a, env_ * envRelease_);
        // "Floor" envelope: follows the detector down at once and up slowly, so
        // only a sudden rise (a new hit on top of a ringing one) is a jump.
        const float floorBefore = slowEnv_;
        slowEnv_ = env_ < slowEnv_ ? env_ : env_ + (slowEnv_ - env_) * slowCoef_;
        tickDet_ = std::max(tickDet_, env_);
        if (sinceLast_ < (1 << 30))
            ++sinceLast_;
        if (env_ < rearm)
        {
            if (++belowCount_ >= rearmHold_)
                armed_ = true;
        }
        else
        {
            belowCount_ = 0;
        }

        if (latency > 0 && mix_ > 0.0f)
        {
            const bool jump = env_ >= kJumpRatio * std::max(floorBefore, 1.0e-6f);
            if (windowLeft_ == 0 && env_ >= thr && (armed_ || jump) && sinceLast_ >= retrigger)
            {
                armed_ = false;
                sinceLast_ = 0;
                windowLeft_ = latency;
                windowPeak_ = 0.0f;
            }
            if (windowLeft_ > 0)
            {
                windowPeak_ = std::max(windowPeak_, a);
                if (--windowLeft_ == 0)
                {
                    const float peakDb = gainToDb(std::max(windowPeak_, 1.0e-9f));
                    const float norm = std::clamp((peakDb - settings_.replaceThresholdDb) / settings_.replaceRangeDb, 0.0f, 1.0f);
                    const float velocity = std::max(0.05f, 1.0f - dynamics * (1.0f - norm));
                    for (auto& p : pending_)
                        if (!p.used)
                        {
                            p.used = true;
                            p.countdown = keyOffset_ + 1;
                            p.velocity = velocity;
                            break;
                        }
                    lastVelocity_ = velocity;
                    ++tickHits_;
                    ++totalHits_;
                }
            }
        }

        // ---- Delay the original and mix ----------------------------------------
        float* slot = delay_.data() + static_cast<size_t>(delayPos_) * kMaxChannels;
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float x = audio[ch][i];
            slot[ch] = std::isfinite(x) ? x : 0.0f;
        }
        const int readPos = (delayPos_ - latency + delayCapacity_) % delayCapacity_;
        const float* delayed = delay_.data() + static_cast<size_t>(readPos) * kMaxChannels;
        delayPos_ = (delayPos_ + 1) % delayCapacity_;

        float vl = 0.0f, vr = 0.0f;
        renderVoices(&vl, &vr, nCh);
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float dry = delayed[ch];
            const float smp = nCh == 1 ? 0.5f * (vl + vr) : (ch == 0 ? vl : ch == 1 ? vr : 0.5f * (vl + vr));
            const float wet = dry * originalGain_ + smp * sampleGain_;
            audio[ch][i] = (dry + mix_ * (wet - dry)) * fade_;
        }

        // ---- Telemetry -----------------------------------------------------------
        if (++tick_ >= hop_)
        {
            tick_ = 0;
            int voices = 0;
            for (const auto& v : voices_)
                voices += v.sample != nullptr ? 1 : 0;
            snap_.enabled = active;
            snap_.kitLoaded = kit_ != nullptr && !kit_->empty();
            snap_.detectorDb = gainToDb(tickDet_);
            snap_.thresholdDb = settings_.replaceThresholdDb;
            snap_.hitsThisTick = tickHits_;
            snap_.lastVelocity = lastVelocity_;
            snap_.totalHits = totalHits_;
            hitsLastSecond_ += tickHits_ - hitRing_[static_cast<size_t>(hitRingPos_)];
            hitRing_[static_cast<size_t>(hitRingPos_)] = static_cast<std::uint8_t>(std::min(tickHits_, 255));
            hitRingPos_ = (hitRingPos_ + 1) % static_cast<int>(hitRing_.size());
            secondsSinceHit_ = tickHits_ > 0 ? 0.0f : std::min(1.0e6f, secondsSinceHit_ + 0.01f);
            snap_.hitsLastSecond = hitsLastSecond_;
            snap_.secondsSinceHit = secondsSinceHit_;
            snap_.activeVoices = voices;
            snap_.latencySamples = latency;
            ++snap_.sequence;
            telemetry_.write(snap_);
            tickDet_ = 0.0f;
            tickHits_ = 0;
        }
    }
    kits_.blockDone();
}

} // namespace mixcore::drum
