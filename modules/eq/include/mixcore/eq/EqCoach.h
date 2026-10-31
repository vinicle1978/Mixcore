#pragma once
// EQ Coach: "Listen & suggest".
//
// Listens to the signal going into the EQ for a few seconds of real playing
// (quiet frames are ignored), builds its long-term average spectrum in 1/3
// octaves, and compares it with the typical spectrum of this kind of source
// (voices: a long-term speech/singing shape; drums and instruments: flat).
// Each region is judged against its own neighbours, so overall tilt and level
// never trigger anything. Regions that stick out (or fall short) for this
// kind of source become suggestions, each mapped to one EQ band with
// bounded settings and a plain-language reason. Nothing is applied until the
// user presses Apply. Runs on the message thread only.

#include "mixcore/eq/EqSettings.h"
#include "mixcore/eq/Spectrum.h"
#include "mixcore/gain/SourceType.h"

#include <memory>
#include <string>
#include <vector>

namespace mixcore::eq {

struct EqSuggestion
{
    std::string id;     ///< "hpf-rumble", "resonance", "mud", "harsh", "air", ...
    std::string title;  ///< e.g. "Cut a resonance at 2.1 kHz"
    std::string why;    ///< plain-language reason
    int band = 0;       ///< which EQ band it sets
    EqBand settings;    ///< what that band would be set to (on)
};

class EqListener
{
public:
    static constexpr int kFftOrder = 13; // 8192 samples per frame

    EqListener();

    void start(double seconds, double sampleRate);
    void cancel() noexcept { listening_ = false; }
    bool listening() const noexcept { return listening_; }
    bool ready() const noexcept { return ready_; }
    float progress() const noexcept;
    static int frameSize() noexcept { return 1 << kFftOrder; }

    /// Analyse one frame of frameSize() samples (the caller hops through the
    /// signal, e.g. every half frame). Frames quieter than -55 dBFS RMS are
    /// ignored and do not count towards the listening time.
    void feedFrame(const float* frame);

    /// Long-term 1/3-octave levels (dB) of the frames used (within 20 dB of the
    /// loudest frame). Empty until ready().
    const std::vector<float>& levelsDb() const noexcept { return levels_; }

    /// At most 5 suggestions, at most one per band, strongest first.
    std::vector<EqSuggestion> suggestions(gain::SourceType source, const EqSettings& current) const;

private:
    std::unique_ptr<Fft> fft_;
    double sampleRate_ = 48000.0;
    double seconds_ = 8.0;
    bool listening_ = false, ready_ = false;
    int activeFrames_ = 0;
    struct Frame
    {
        float rmsDb;
        std::vector<float> power; // full-resolution power spectrum
    };
    std::vector<Frame> frames_;
    std::vector<float> levels_;
    std::vector<float> avgPower_;
    void finish();
};

} // namespace mixcore::eq
