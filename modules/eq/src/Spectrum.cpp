#include "mixcore/eq/Spectrum.h"

#include <algorithm>
#include <cmath>

namespace mixcore::eq {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

Fft::Fft(int order) : n_(1 << order), bitrev_(static_cast<size_t>(n_)), twiddle_(static_cast<size_t>(n_ / 2))
{
    for (int i = 0; i < n_; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            if ((i >> b) & 1)
                r |= 1 << (order - 1 - b);
        bitrev_[static_cast<size_t>(i)] = r;
    }
    for (int k = 0; k < n_ / 2; ++k)
        twiddle_[static_cast<size_t>(k)] = std::polar(1.0f, static_cast<float>(-2.0 * kPi * k / n_));
}

void Fft::forward(std::vector<std::complex<float>>& d) const
{
    for (int i = 0; i < n_; ++i)
    {
        const int r = bitrev_[static_cast<size_t>(i)];
        if (r > i)
            std::swap(d[static_cast<size_t>(i)], d[static_cast<size_t>(r)]);
    }
    for (int len = 2; len <= n_; len <<= 1)
    {
        const int step = n_ / len;
        for (int i = 0; i < n_; i += len)
            for (int j = 0; j < len / 2; ++j)
            {
                const auto w = twiddle_[static_cast<size_t>(j * step)];
                auto& a = d[static_cast<size_t>(i + j)];
                auto& b = d[static_cast<size_t>(i + j + len / 2)];
                const auto t = w * b;
                b = a - t;
                a = a + t;
            }
    }
}

std::vector<float> powerSpectrum(const float* samples, const Fft& fft)
{
    const int n = fft.size();
    std::vector<std::complex<float>> d(static_cast<size_t>(n));
    double wsum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n - 1));
        wsum += w;
        d[static_cast<size_t>(i)] = { static_cast<float>(static_cast<double>(samples[i]) * w), 0.0f };
    }
    fft.forward(d);
    std::vector<float> p(static_cast<size_t>(n / 2 + 1));
    const double scale = 2.0 / wsum; // amplitude of a sine in its bin -> ~1
    for (int k = 0; k <= n / 2; ++k)
    {
        const double a = static_cast<double>(std::abs(d[static_cast<size_t>(k)])) * scale;
        p[static_cast<size_t>(k)] = static_cast<float>(a * a);
    }
    return p;
}

const std::vector<double>& thirdOctaveCentres()
{
    static const std::vector<double> c = [] {
        std::vector<double> v;
        for (int i = -17; i <= 13; ++i) // 1000 * 2^(i/3): 20 Hz .. 20 kHz
            v.push_back(1000.0 * std::pow(2.0, i / 3.0));
        return v;
    }();
    return c;
}

std::vector<float> thirdOctaveLevelsDb(const std::vector<float>& power, double sampleRate)
{
    const auto& c = thirdOctaveCentres();
    const auto nBins = static_cast<int>(power.size());
    const double binHz = sampleRate / (2.0 * (nBins - 1));
    std::vector<float> out(c.size(), -150.0f);
    const double edge = std::pow(2.0, 1.0 / 6.0);
    for (size_t b = 0; b < c.size(); ++b)
    {
        const double lo = c[b] / edge, hi = std::min(c[b] * edge, 0.5 * sampleRate);
        double sum = 0.0;
        int count = 0;
        for (int k = std::max(1, static_cast<int>(std::ceil(lo / binHz))); k < nBins && k * binHz < hi; ++k)
        {
            sum += static_cast<double>(power[static_cast<size_t>(k)]);
            ++count;
        }
        if (count == 0) // band narrower than a bin: use the nearest bin
        {
            const int k = static_cast<int>(std::lround(c[b] / binHz));
            if (k > 0 && k < nBins && c[b] < 0.5 * sampleRate)
                sum = static_cast<double>(power[static_cast<size_t>(k)]) * (hi - lo) / binHz;
        }
        out[b] = static_cast<float>(10.0 * std::log10(std::max(sum, 1.0e-15)));
    }
    return out;
}

std::vector<float> smoothedSpectrumDb(const std::vector<float>& power, double sampleRate, const std::vector<double>& freqs,
                                      double octaveWidth)
{
    const auto nBins = static_cast<int>(power.size());
    const double binHz = sampleRate / (2.0 * (nBins - 1));
    const double half = std::pow(2.0, octaveWidth * 0.5);
    std::vector<float> out(freqs.size(), -150.0f);
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        const int k0 = std::max(1, static_cast<int>(std::floor(freqs[i] / half / binHz)));
        const int k1 = std::min(nBins - 1, std::max(k0, static_cast<int>(std::ceil(freqs[i] * half / binHz))));
        double sum = 0.0;
        for (int k = k0; k <= k1; ++k)
            sum += static_cast<double>(power[static_cast<size_t>(k)]);
        out[i] = static_cast<float>(10.0 * std::log10(std::max(sum / (k1 - k0 + 1), 1.0e-15)));
    }
    return out;
}

} // namespace mixcore::eq
