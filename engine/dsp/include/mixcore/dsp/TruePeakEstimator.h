#pragma once
// 4x oversampled inter-sample peak estimator.
//
// A 48-tap windowed-sinc interpolator (12 taps per polyphase branch) is
// designed at construction. This follows the approach described in
// ITU-R BS.1770 Annex 2 but uses MixCore's own filter, so results are an
// *estimate* (typically within ~0.5 dB of a compliant meter at audio
// frequencies) and are labelled "estimated true peak" in the UI.

#include <array>
#include <vector>

namespace mixcore::dsp {

class TruePeakEstimator
{
public:
    static constexpr int kOversampling = 4;
    static constexpr int kTapsPerPhase = 12;

    TruePeakEstimator();

    /// Allocates history for `numChannels`. Non-realtime.
    void prepare(int numChannels);
    void reset() noexcept;

    /// Feed one sample for `channel`; returns the largest absolute value among
    /// the 4 interpolated points ending at this sample (linear, not dB).
    float process(int channel, float x) noexcept;

    /// Group delay of the interpolator, in input samples (for reference only).
    static constexpr double groupDelaySamples() noexcept
    {
        return (kOversampling * kTapsPerPhase - 1) / 2.0 / kOversampling;
    }

private:
    std::array<std::array<float, kTapsPerPhase>, kOversampling> phases_ {};
    // Per channel: doubled ring buffer so the newest kTapsPerPhase samples are
    // always contiguous.
    std::vector<std::array<float, 2 * kTapsPerPhase>> history_;
    std::vector<int> position_;
};

} // namespace mixcore::dsp
