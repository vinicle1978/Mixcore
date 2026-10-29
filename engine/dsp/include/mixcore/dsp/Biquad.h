#pragma once
// Transposed direct-form II biquad (double state). Coefficients follow the
// RBJ Audio EQ Cookbook. Only the shapes needed so far are provided; the
// parametric EQ module (Milestone 4) will extend this.

#include <cmath>

namespace mixcore::dsp {

class Biquad
{
public:
    void setHighPass(double sampleRate, double frequencyHz, double q) noexcept
    {
        const double w0 = 2.0 * 3.14159265358979323846 * frequencyHz / sampleRate;
        const double cosw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0_ = ((1.0 + cosw) * 0.5) / a0;
        b1_ = -(1.0 + cosw) / a0;
        b2_ = b0_;
        a1_ = (-2.0 * cosw) / a0;
        a2_ = (1.0 - alpha) / a0;
    }

    void setLowPass(double sampleRate, double frequencyHz, double q) noexcept
    {
        const double w0 = 2.0 * 3.14159265358979323846 * frequencyHz / sampleRate;
        const double cosw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0_ = ((1.0 - cosw) * 0.5) / a0;
        b1_ = (1.0 - cosw) / a0;
        b2_ = b0_;
        a1_ = (-2.0 * cosw) / a0;
        a2_ = (1.0 - alpha) / a0;
    }

    void reset() noexcept { z1_ = z2_ = 0.0; }

    float process(float x) noexcept
    {
        const double in = static_cast<double>(x);
        const double y = b0_ * in + z1_;
        z1_ = b1_ * in - a1_ * y + z2_;
        z2_ = b2_ * in - a2_ * y;
        return static_cast<float>(y);
    }

private:
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
    double z1_ = 0.0, z2_ = 0.0;
};

} // namespace mixcore::dsp
