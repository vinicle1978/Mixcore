#pragma once
// Filter design for the parametric EQ (RBJ Audio EQ Cookbook, double precision)
// and the exact magnitude response used by the UI curve and by tests.
//
// A band is up to 4 biquad sections:
//   Bell / shelves          1 section
//   High/low-pass 12 dB/oct 1 section, user Q (0.707 = Butterworth)
//   High/low-pass 24 dB/oct 2 sections, Butterworth (Q 0.5412, 1.3066)
//   High/low-pass 48 dB/oct 4 sections, Butterworth

#include "mixcore/eq/EqSettings.h"

#include <array>
#include <cmath>
#include <complex>

namespace mixcore::eq {

struct Coefs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    /// |H(e^jw)| at `freq` for sample rate `sr`.
    double magnitude(double freq, double sr) const noexcept
    {
        const double w = 2.0 * 3.14159265358979323846 * freq / sr;
        const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return std::abs(num / den);
    }
};

struct BandDesign
{
    std::array<Coefs, 4> sections {};
    int count = 1;
};

namespace detail {
inline Coefs normalise(double b0, double b1, double b2, double a0, double a1, double a2) noexcept
{
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}
} // namespace detail

inline Coefs designPeak(double sr, double f, double q, double gainDb) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * 3.14159265358979323846 * f / sr;
    const double alpha = std::sin(w) / (2.0 * q);
    const double c = std::cos(w);
    return detail::normalise(1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
}

inline Coefs designLowShelf(double sr, double f, double q, double gainDb) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * 3.14159265358979323846 * f / sr;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q), sA = 2.0 * std::sqrt(A) * alpha;
    return detail::normalise(A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
                             (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
}

inline Coefs designHighShelf(double sr, double f, double q, double gainDb) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * 3.14159265358979323846 * f / sr;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q), sA = 2.0 * std::sqrt(A) * alpha;
    return detail::normalise(A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                             (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
}

inline Coefs designHighPass(double sr, double f, double q) noexcept
{
    const double w = 2.0 * 3.14159265358979323846 * f / sr;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    return detail::normalise((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

inline Coefs designLowPass(double sr, double f, double q) noexcept
{
    const double w = 2.0 * 3.14159265358979323846 * f / sr;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    return detail::normalise((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

/// Designs one band (frequency is limited to 0.45 x the sample rate).
inline BandDesign designBand(BandType type, double sr, double freq, double q, double gainDb, double slope) noexcept
{
    BandDesign d;
    const double f = std::min(freq, 0.45 * sr);
    switch (type)
    {
        case BandType::Bell: d.sections[0] = designPeak(sr, f, q, gainDb); break;
        case BandType::LowShelf: d.sections[0] = designLowShelf(sr, f, std::min(q, 2.0), gainDb); break;
        case BandType::HighShelf: d.sections[0] = designHighShelf(sr, f, std::min(q, 2.0), gainDb); break;
        case BandType::HighPass:
        case BandType::LowPass:
        {
            static constexpr double q24[] = { 0.54119610, 1.30656296 };
            static constexpr double q48[] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
            const bool hp = type == BandType::HighPass;
            const auto make = [&](double qq) { return hp ? designHighPass(sr, f, qq) : designLowPass(sr, f, qq); };
            if (slope < 18.0)
            {
                d.count = 1;
                d.sections[0] = make(q);
            }
            else if (slope < 36.0)
            {
                d.count = 2;
                for (int i = 0; i < 2; ++i)
                    d.sections[static_cast<size_t>(i)] = make(q24[i]);
            }
            else
            {
                d.count = 4;
                for (int i = 0; i < 4; ++i)
                    d.sections[static_cast<size_t>(i)] = make(q48[i]);
            }
            break;
        }
    }
    return d;
}

/// Exact response (dB) of the whole EQ at `freq`, including output gain; 0 dB when disabled.
inline double responseDb(const EqSettings& s, double sr, double freq) noexcept
{
    if (!s.enabled)
        return 0.0;
    double mag = std::pow(10.0, static_cast<double>(s.outputDb) / 20.0);
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& b = s.bands[static_cast<size_t>(i)];
        if (!b.on)
            continue;
        const auto d = designBand(EqSettings::type(i), sr, static_cast<double>(b.freqHz), static_cast<double>(b.q),
                                  static_cast<double>(b.gainDb), static_cast<double>(b.slope));
        for (int k = 0; k < d.count; ++k)
            mag *= d.sections[static_cast<size_t>(k)].magnitude(freq, sr);
    }
    return 20.0 * std::log10(std::max(mag, 1.0e-12));
}

/// Response (dB) of a single band (for drawing each band's own curve).
inline double bandResponseDb(const EqSettings& s, int band, double sr, double freq) noexcept
{
    const auto& b = s.bands[static_cast<size_t>(band)];
    const auto d = designBand(EqSettings::type(band), sr, static_cast<double>(b.freqHz), static_cast<double>(b.q),
                              static_cast<double>(b.gainDb), static_cast<double>(b.slope));
    double mag = 1.0;
    for (int k = 0; k < d.count; ++k)
        mag *= d.sections[static_cast<size_t>(k)].magnitude(freq, sr);
    return 20.0 * std::log10(std::max(mag, 1.0e-12));
}

} // namespace mixcore::eq
