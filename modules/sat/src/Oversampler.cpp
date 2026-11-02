#include "mixcore/sat/Oversampler.h"

#include <algorithm>
#include <cmath>

namespace mixcore::sat {

namespace {
double besselI0(double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}
} // namespace

void HalfbandStage::design(int taps, double beta) noexcept
{
    taps_ = std::clamp(taps | 1, 3, kMaxTaps);
    const double c = (taps_ - 1) * 0.5;
    double sum = 0.0;
    for (int n = 0; n < taps_; ++n)
    {
        const double t = n - c;
        const double sinc = t == 0.0 ? 0.5 : std::sin(3.141592653589793 * 0.5 * t) / (3.141592653589793 * t);
        const double r = t / c;
        const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
        h_[static_cast<size_t>(n)] = sinc * w;
        sum += h_[static_cast<size_t>(n)];
    }
    for (int n = 0; n < taps_; ++n)
        h_[static_cast<size_t>(n)] /= sum; // unity gain at DC
    reset();
}

void HalfbandStage::reset() noexcept
{
    upHist_.fill(0.0f);
    downHist_.fill(0.0f);
    upPos_ = downPos_ = 0;
}

void HalfbandStage::up(float x, float& out0, float& out1) noexcept
{
    // Doubled circular buffer: the newest sample sits at upPos_ and upPos_ + taps_.
    upPos_ = (upPos_ == 0 ? taps_ : upPos_) - 1;
    upHist_[static_cast<size_t>(upPos_)] = x;
    upHist_[static_cast<size_t>(upPos_ + taps_)] = x;
    const float* hist = upHist_.data() + upPos_; // hist[k] = x[m - k]
    double a = 0.0, b = 0.0;
    for (int k = 0; 2 * k < taps_; ++k)
        a += h_[static_cast<size_t>(2 * k)] * static_cast<double>(hist[k]);
    for (int k = 0; 2 * k + 1 < taps_; ++k)
        b += h_[static_cast<size_t>(2 * k + 1)] * static_cast<double>(hist[k]);
    out0 = static_cast<float>(2.0 * a);
    out1 = static_cast<float>(2.0 * b);
}

float HalfbandStage::down(float in0, float in1) noexcept
{
    const auto push = [this](float v) {
        downPos_ = (downPos_ == 0 ? taps_ : downPos_) - 1;
        downHist_[static_cast<size_t>(downPos_)] = v;
        downHist_[static_cast<size_t>(downPos_ + taps_)] = v;
    };
    push(in0);
    const float* hist = downHist_.data() + downPos_; // hist[n] = v[j - n]
    double z = 0.0;
    for (int n = 0; n < taps_; ++n)
        z += h_[static_cast<size_t>(n)] * static_cast<double>(hist[n]);
    push(in1);
    return static_cast<float>(z);
}

Oversampler4x::Oversampler4x()
{
    s1_.design(47, 8.0);
    s2_.design(21, 8.0);
}

void Oversampler4x::reset() noexcept
{
    s1_.reset();
    s2_.reset();
}

void Oversampler4x::up(float x, float* out4) noexcept
{
    float a = 0.0f, b = 0.0f;
    s1_.up(x, a, b);
    s2_.up(a, out4[0], out4[1]);
    s2_.up(b, out4[2], out4[3]);
}

float Oversampler4x::down(const float* in4) noexcept
{
    const float a = s2_.down(in4[0], in4[1]);
    const float b = s2_.down(in4[2], in4[3]);
    return s1_.down(a, b);
}

} // namespace mixcore::sat
