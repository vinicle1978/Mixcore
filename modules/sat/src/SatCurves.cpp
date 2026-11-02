#include "mixcore/sat/SatCurves.h"

#include "mixcore/core/DbMath.h"

#include <algorithm>
#include <cmath>

namespace mixcore::sat {

namespace {
constexpr float kTubeBias = 0.15f; // gentle asymmetry: about -30 dB 2nd harmonic at -8 dBFS peaks with no drive
const float kTubeOffset = std::tanh(kTubeBias);
const float kTubeSlope = 1.0f - kTubeOffset * kTubeOffset; // sech^2(bias)
constexpr int kPoints = 512;                               // samples per analysed period
constexpr double kTwoPi = 6.283185307179586;
} // namespace

float shape(SatType type, float x) noexcept
{
    switch (type)
    {
        case SatType::Tube:
            // Biased tanh: the positive and negative halves clip differently -> even harmonics.
            return (std::tanh(x + kTubeBias) - kTubeOffset) / kTubeSlope;
        case SatType::Console:
        {
            // Cubic soft clip, slope 1 at 0, flat beyond |x| = 1.5.
            const float c = std::clamp(x, -1.5f, 1.5f);
            return c - (4.0f / 27.0f) * c * c * c;
        }
        case SatType::Clip: return std::clamp(x, -1.0f, 1.0f);
        case SatType::Tape: break;
    }
    return std::tanh(x);
}

std::array<float, 7> harmonicsDb(SatType type, float peakDb, float driveDb) noexcept
{
    const double a = static_cast<double>(dbToGain(peakDb + driveDb));
    std::array<double, 9> mag {};
    for (int k = 1; k <= 8; ++k)
    {
        double re = 0.0, im = 0.0;
        for (int n = 0; n < kPoints; ++n)
        {
            const double ph = kTwoPi * n / kPoints;
            const double y = static_cast<double>(shape(type, static_cast<float>(a * std::sin(ph))));
            re += y * std::cos(k * ph);
            im += y * std::sin(k * ph);
        }
        mag[static_cast<size_t>(k)] = std::sqrt(re * re + im * im);
    }
    std::array<float, 7> out {};
    for (int k = 2; k <= 8; ++k)
        out[static_cast<size_t>(k - 2)] = mag[1] > 0.0 ? std::max(-150.0f, static_cast<float>(20.0 * std::log10(mag[static_cast<size_t>(k)] / mag[1] + 1e-12)))
                                                       : -150.0f;
    return out;
}

float thdDb(SatType type, float peakDb, float driveDb) noexcept
{
    double p = 0.0;
    for (float h : harmonicsDb(type, peakDb, driveDb))
        p += std::pow(10.0, static_cast<double>(h) / 10.0);
    return p > 1e-15 ? static_cast<float>(10.0 * std::log10(p)) : -150.0f;
}

float driveForThd(SatType type, float peakDb, float targetThdDb) noexcept
{
    float lo = 0.0f, hi = 36.0f;
    if (thdDb(type, peakDb, hi) < targetThdDb)
        return hi;
    if (thdDb(type, peakDb, lo) >= targetThdDb)
        return lo;
    for (int i = 0; i < 30; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        (thdDb(type, peakDb, mid) < targetThdDb ? lo : hi) = mid;
    }
    return 0.5f * (lo + hi);
}

} // namespace mixcore::sat
