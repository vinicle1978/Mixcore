#include "mixcore/gate/GateLearner.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mixcore::gate {

void GateLearner::prepare(double tickSeconds, double maxSeconds)
{
    tickSeconds_ = tickSeconds > 0.0 ? tickSeconds : 0.01;
    levels_.assign(static_cast<size_t>(std::max(1.0, maxSeconds / tickSeconds_)), -150.0f);
    count_ = 0;
    state_.store(LearnState::Idle, std::memory_order_release);
    progress_.store(0.0f);
}

void GateLearner::requestStart(double seconds) noexcept
{
    const int required = static_cast<int>(std::lround(std::clamp(seconds, 2.0, 20.0) / tickSeconds_));
    pendingRequired_.store(std::clamp(required, 1, static_cast<int>(levels_.size())), std::memory_order_relaxed);
    progress_.store(0.0f, std::memory_order_relaxed);
    state_.store(LearnState::Requested, std::memory_order_release);
}

void GateLearner::cancel() noexcept
{
    state_.store(LearnState::Idle, std::memory_order_release);
    progress_.store(0.0f, std::memory_order_relaxed);
}

void GateLearner::onTick(float detectorDb) noexcept
{
    auto s = state_.load(std::memory_order_acquire);
    if (s == LearnState::Requested)
    {
        count_ = 0;
        required_ = pendingRequired_.load(std::memory_order_relaxed);
        if (!state_.compare_exchange_strong(s, LearnState::Listening, std::memory_order_acq_rel))
            return;
        s = LearnState::Listening;
    }
    if (s != LearnState::Listening)
        return;

    if (count_ < static_cast<int>(levels_.size()))
        levels_[static_cast<size_t>(count_++)] = std::isfinite(detectorDb) ? detectorDb : -150.0f;
    progress_.store(static_cast<float>(count_) / static_cast<float>(required_), std::memory_order_relaxed);
    if (count_ >= required_)
        state_.compare_exchange_strong(s, LearnState::Ready, std::memory_order_acq_rel);
}

LearnResult GateLearner::computeResult(bool percussive) const
{
    if (state() != LearnState::Ready)
    {
        LearnResult r;
        r.summary = "Learning has not finished.";
        return r;
    }
    return analyse(std::vector<float>(levels_.begin(), levels_.begin() + count_), percussive);
}

LearnResult GateLearner::analyse(std::vector<float> v, bool percussive)
{
    LearnResult r;
    // Ignore digital silence (e.g. a muted input): it is not "bleed".
    v.erase(std::remove_if(v.begin(), v.end(), [](float x) { return !(x > -120.0f); }), v.end());
    if (v.size() < 50)
    {
        r.summary = "No signal was detected. Check that the channel is receiving audio and try again.";
        return r;
    }
    std::sort(v.begin(), v.end());
    const size_t n = v.size();

    // Otsu split on the sorted levels using prefix sums.
    std::vector<double> prefix(n + 1, 0.0);
    for (size_t i = 0; i < n; ++i)
        prefix[i + 1] = prefix[i] + static_cast<double>(v[i]);
    double best = -1.0;
    size_t split = n / 2;
    for (size_t k = 1; k < n; ++k)
    {
        const double w0 = static_cast<double>(k) / static_cast<double>(n);
        const double w1 = 1.0 - w0;
        const double m0 = prefix[k] / static_cast<double>(k);
        const double m1 = (prefix[n] - prefix[k]) / static_cast<double>(n - k);
        const double between = w0 * w1 * (m0 - m1) * (m0 - m1);
        if (between > best)
        {
            best = between;
            split = k;
        }
    }
    auto percentileOf = [&v](size_t lo, size_t hi, double q) {
        const size_t idx = lo + static_cast<size_t>(q * static_cast<double>(hi - lo - 1));
        return v[idx];
    };
    r.bleedDb = percentileOf(0, split, 0.5); // median of the quiet group
    r.sourceDb = percussive ? percentileOf(split, n, 0.9)  // hit peaks
                            : percentileOf(split, n, 0.5); // typical level
    r.separationDb = r.sourceDb - r.bleedDb;

    float suggestion = r.bleedDb + 0.4f * r.separationDb;
    if (percussive)
        suggestion = std::max(suggestion, r.sourceDb - 18.0f);
    suggestion = std::max(suggestion, r.bleedDb + std::min(6.0f, 0.5f * r.separationDb));
    suggestion = std::min(suggestion, r.sourceDb - std::min(6.0f, 0.5f * r.separationDb));
    r.suggestedThresholdDb = std::clamp(suggestion, -80.0f, 0.0f);
    r.suggestedHysteresisDb = std::clamp(0.2f * r.separationDb, 3.0f, 10.0f);

    std::ostringstream msg;
    msg.setf(std::ios::fixed);
    msg.precision(1);
    msg << (percussive ? "Hits peak around " : "The source sits around ") << r.sourceDb << " dBFS; between hits/phrases the mic picks up about " << r.bleedDb
        << " dBFS (" << r.separationDb << " dB apart). ";
    if (r.separationDb >= 15.0f)
    {
        r.confidence = LearnResult::Confidence::Good;
        r.valid = true;
        msg << "Suggested threshold " << r.suggestedThresholdDb << " dBFS with " << r.suggestedHysteresisDb
            << " dB hysteresis. Good separation: the gate should open reliably and stay shut on bleed.";
    }
    else if (r.separationDb >= 8.0f)
    {
        r.confidence = LearnResult::Confidence::Low;
        r.valid = true;
        msg << "Suggested threshold " << r.suggestedThresholdDb
            << " dBFS, but separation is small: quiet notes may be cut or loud bleed may open the gate. "
               "Consider a smaller range (gentler), moving the mic closer, or turning on transient trigger for drums.";
    }
    else
    {
        r.confidence = LearnResult::Confidence::None;
        msg << "The bleed is almost as loud as the source, so no threshold can separate them reliably. "
               "Reposition or change the mic, or leave the gate off on this channel.";
    }
    r.summary = msg.str();
    return r;
}

} // namespace mixcore::gate
