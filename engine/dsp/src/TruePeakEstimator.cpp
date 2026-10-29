#include "mixcore/dsp/TruePeakEstimator.h"

#include <algorithm>
#include <cmath>

namespace mixcore::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;

double sinc(double x)
{
    if (std::abs(x) < 1.0e-12)
        return 1.0;
    return std::sin(kPi * x) / (kPi * x);
}
} // namespace

TruePeakEstimator::TruePeakEstimator()
{
    constexpr int totalTaps = kOversampling * kTapsPerPhase;
    const double centre = (totalTaps - 1) / 2.0;

    std::array<double, totalTaps> h {};
    for (int n = 0; n < totalTaps; ++n)
    {
        // 4-term Blackman-Harris window.
        const double r = 2.0 * kPi * n / (totalTaps - 1);
        const double window = 0.35875 - 0.48829 * std::cos(r) + 0.14128 * std::cos(2.0 * r) - 0.01168 * std::cos(3.0 * r);
        h[n] = sinc((n - centre) / kOversampling) * window;
    }

    // y[4n + p] = sum_k h[p + 4k] * x[n - k]; normalise each branch to unity DC gain.
    for (int p = 0; p < kOversampling; ++p)
    {
        double sum = 0.0;
        for (int k = 0; k < kTapsPerPhase; ++k)
            sum += h[p + kOversampling * k];
        for (int k = 0; k < kTapsPerPhase; ++k)
            phases_[p][k] = static_cast<float>(h[p + kOversampling * k] / sum);
    }
}

void TruePeakEstimator::prepare(int numChannels)
{
    history_.assign(static_cast<size_t>(std::max(1, numChannels)), {});
    position_.assign(history_.size(), 0);
    reset();
}

void TruePeakEstimator::reset() noexcept
{
    for (auto& h : history_)
        h.fill(0.0f);
    std::fill(position_.begin(), position_.end(), 0);
}

float TruePeakEstimator::process(int channel, float x) noexcept
{
    if (channel < 0 || channel >= static_cast<int>(history_.size()))
        return std::abs(x);

    auto& hist = history_[static_cast<size_t>(channel)];
    int& pos = position_[static_cast<size_t>(channel)];

    // Write newest sample at pos and pos + taps; newest-first view starts at pos.
    pos = (pos == 0) ? kTapsPerPhase - 1 : pos - 1;
    hist[static_cast<size_t>(pos)] = x;
    hist[static_cast<size_t>(pos + kTapsPerPhase)] = x;
    const float* newestFirst = hist.data() + pos; // newestFirst[k] == x[n - k]

    float peak = 0.0f;
    for (int p = 0; p < kOversampling; ++p)
    {
        float acc = 0.0f;
        for (int k = 0; k < kTapsPerPhase; ++k)
            acc += phases_[p][k] * newestFirst[k];
        peak = std::max(peak, std::abs(acc));
    }
    return peak;
}

} // namespace mixcore::dsp
