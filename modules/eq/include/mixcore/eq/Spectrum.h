#pragma once
// FFT and spectrum helpers for the analyzer and the EQ Coach (message thread;
// these allocate and are never used on the audio thread).

#include <complex>
#include <vector>

namespace mixcore::eq {

/// Iterative radix-2 complex FFT.
class Fft
{
public:
    explicit Fft(int order); ///< size = 2^order
    int size() const noexcept { return n_; }
    void forward(std::vector<std::complex<float>>& data) const;

private:
    int n_;
    std::vector<int> bitrev_;
    std::vector<std::complex<float>> twiddle_;
};

/// Hann-windowed power spectrum of `n` = fft.size() samples (n/2 + 1 bins),
/// scaled so a full-scale sine reads about 0 dB in its bin.
std::vector<float> powerSpectrum(const float* samples, const Fft& fft);

/// ISO 1/3-octave centres from 20 Hz to 20 kHz (31 bands).
const std::vector<double>& thirdOctaveCentres();

/// Power per 1/3-octave band, in dB (bins beyond Nyquist are ignored; empty
/// bands read -150 dB).
std::vector<float> thirdOctaveLevelsDb(const std::vector<float>& power, double sampleRate);

/// Smoothed spectrum in dB at the given display frequencies: power averaged
/// over +-`octaveWidth`/2 around each frequency.
std::vector<float> smoothedSpectrumDb(const std::vector<float>& power, double sampleRate, const std::vector<double>& freqs,
                                      double octaveWidth = 1.0 / 6.0);

} // namespace mixcore::eq
