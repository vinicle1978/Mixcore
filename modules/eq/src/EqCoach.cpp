#include "mixcore/eq/EqCoach.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>

namespace mixcore::eq {

namespace {
constexpr float kSilentRmsDb = -55.0f;
constexpr float kUseWithinDb = 20.0f;
constexpr size_t kMaxFrames = 600;

std::string hz(double f)
{
    char buf[32];
    if (f >= 1000.0)
        std::snprintf(buf, sizeof buf, "%.1f kHz", f / 1000.0);
    else
        std::snprintf(buf, sizeof buf, "%d Hz", static_cast<int>(std::lround(f)));
    return buf;
}

std::string db(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%+.1f dB", v);
    return buf;
}

int bellFor(double f) { return f < 600.0 ? 2 : (f < 2500.0 ? 3 : 4); }
} // namespace

EqListener::EqListener() : fft_(std::make_unique<Fft>(kFftOrder)) {}

void EqListener::start(double seconds, double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    seconds_ = std::max(1.0, seconds);
    frames_.clear();
    levels_.clear();
    avgPower_.clear();
    activeFrames_ = 0;
    ready_ = false;
    listening_ = true;
}

float EqListener::progress() const noexcept
{
    if (ready_)
        return 1.0f;
    const double hop = 0.5 * frameSize() / sampleRate_;
    return static_cast<float>(std::min(1.0, activeFrames_ * hop / seconds_));
}

void EqListener::feedFrame(const float* frame)
{
    if (!listening_)
        return;
    const int n = frameSize();
    double sq = 0.0;
    for (int i = 0; i < n; ++i)
        sq += static_cast<double>(frame[i]) * static_cast<double>(frame[i]);
    // Same convention as the rest of MixCore (full-scale sine = -3 dBFS).
    const float rmsDb = static_cast<float>(10.0 * std::log10(std::max(sq / n, 1.0e-20)));
    if (rmsDb < kSilentRmsDb)
        return;
    if (frames_.size() < kMaxFrames)
        frames_.push_back({ rmsDb, powerSpectrum(frame, *fft_) });
    ++activeFrames_;
    if (progress() >= 1.0f)
        finish();
}

void EqListener::finish()
{
    listening_ = false;
    if (frames_.empty())
        return;
    float loudest = -1000.0f;
    for (const auto& f : frames_)
        loudest = std::max(loudest, f.rmsDb);
    avgPower_.assign(frames_.front().power.size(), 0.0f);
    int used = 0;
    for (const auto& f : frames_)
        if (f.rmsDb >= loudest - kUseWithinDb)
        {
            for (size_t k = 0; k < avgPower_.size(); ++k)
                avgPower_[k] += f.power[k];
            ++used;
        }
    for (auto& p : avgPower_)
        p /= static_cast<float>(std::max(1, used));
    levels_ = thirdOctaveLevelsDb(avgPower_, sampleRate_);
    frames_.clear();
    frames_.shrink_to_fit();
    ready_ = true;
}

std::vector<EqSuggestion> EqListener::suggestions(gain::SourceType source, const EqSettings& current) const
{
    std::vector<EqSuggestion> out;
    if (!ready_ || levels_.empty())
        return out;
    const auto& fc = thirdOctaveCentres();
    const double nyq = 0.5 * sampleRate_;
    const bool voice = source == gain::SourceType::Vocal || source == gain::SourceType::Speech;
    const bool drum = source == gain::SourceType::Drum;

    // Reference: the typical long-term spectrum of this kind of source (voices
    // have their energy around 200-500 Hz and fall off above 1 kHz; drums and
    // instruments are judged against a flat reference). Residual = measured -
    // reference; each region is then judged against its own neighbours (one
    // octave either side), so an overall tilt or level never triggers anything.
    struct RefPoint
    {
        double f, db;
    };
    static const std::vector<RefPoint> vocalRef { { 63, -14 }, { 125, -4 }, { 250, 0 }, { 500, -1 }, { 1000, -5 },
                                                  { 2000, -8 }, { 4000, -11 }, { 8000, -16 }, { 16000, -22 } };
    static const std::vector<RefPoint> speechRef { { 63, -14 }, { 125, -3 }, { 250, 0 }, { 500, -1 }, { 1000, -6 },
                                                   { 2000, -10 }, { 4000, -14 }, { 8000, -19 }, { 16000, -26 } };
    const auto* ref = source == gain::SourceType::Vocal ? &vocalRef : (source == gain::SourceType::Speech ? &speechRef : nullptr);
    const auto refDb = [&](double f) {
        if (ref == nullptr)
            return 0.0;
        const auto& r = *ref;
        if (f <= r.front().f)
            return r.front().db;
        if (f >= r.back().f)
            return r.back().db;
        for (size_t i = 0; i + 1 < r.size(); ++i)
            if (f <= r[i + 1].f)
            {
                const double t = std::log2(f / r[i].f) / std::log2(r[i + 1].f / r[i].f);
                return r[i].db + t * (r[i + 1].db - r[i].db);
            }
        return 0.0;
    };
    const auto excess = [&](size_t k) { return static_cast<double>(levels_[k]) - refDb(fc[k]); };
    const auto meanExcess = [&](double lo, double hi) {
        double s = 0;
        int c = 0;
        for (size_t k = 0; k < fc.size(); ++k)
            if (fc[k] >= lo && fc[k] <= hi && fc[k] < 0.9 * nyq)
                s += excess(k), ++c;
        return c > 0 ? s / c : 0.0;
    };
    const auto meanLevel = [&](double lo, double hi) {
        double s = 0;
        int c = 0;
        for (size_t k = 0; k < fc.size(); ++k)
            if (fc[k] >= lo && fc[k] <= hi && fc[k] < 0.9 * nyq)
                s += static_cast<double>(levels_[k]), ++c;
        return c > 0 ? s / c : -150.0;
    };

    std::map<int, EqSuggestion> byBand;
    const auto offer = [&](EqSuggestion s) {
        auto it = byBand.find(s.band);
        if (it == byBand.end() || std::abs(s.settings.gainDb) > std::abs(it->second.settings.gainDb))
            byBand[s.band] = std::move(s);
    };
    const auto band = [](float f, float g, float q, float passSlope = 24.0f) {
        EqBand b;
        b.on = true;
        b.freqHz = f;
        b.gainDb = g;
        b.q = q;
        b.slope = passSlope;
        return b;
    };

    // 1. Rumble / low-frequency noise -> high-pass.
    const double low = meanLevel(20.0, 63.0), body = meanLevel(200.0, 1000.0);
    const float hpTarget = source == gain::SourceType::Speech ? 100.0f
                           : source == gain::SourceType::Vocal ? 90.0f
                           : drum                              ? 30.0f
                                                               : 50.0f;
    const bool hpfAlready = current.bands[0].on && current.bands[0].freqHz >= hpTarget * 0.9f;
    if (!hpfAlready && low > body - (voice ? 18.0 : 10.0))
    {
        EqSuggestion s;
        s.id = "hpf-rumble";
        s.band = 0;
        s.settings = band(hpTarget, 0.0f, 0.707f, 24.0f);
        s.title = "High-pass at " + hz(static_cast<double>(hpTarget));
        s.why = "There is " + std::string(low > body ? "strong" : "noticeable") + " energy below 63 Hz (" + db(low - body) +
                " relative to the body of the sound). For this source it is mostly rumble, handling noise or stage "
                "spill, and it eats headroom.";
        offer(s);
    }

    // 2. Narrow resonances: a 1/3-octave band 4+ dB above the median of its +-1 octave neighbours.
    struct Peak
    {
        double f, height;
    };
    std::vector<Peak> peaks;
    for (size_t k = 0; k < fc.size(); ++k)
    {
        if (fc[k] < 150.0 || fc[k] > std::min(8000.0, 0.8 * nyq))
            continue;
        std::vector<float> nb;
        for (size_t j = 0; j < fc.size(); ++j)
            if (j != k && std::abs(static_cast<int>(j) - static_cast<int>(k)) <= 3)
                nb.push_back(levels_[j]);
        std::nth_element(nb.begin(), nb.begin() + static_cast<std::ptrdiff_t>(nb.size() / 2), nb.end());
        const double h = static_cast<double>(levels_[k]) - static_cast<double>(nb[nb.size() / 2]);
        const bool localMax = (k == 0 || levels_[k] >= levels_[k - 1]) && (k + 1 >= fc.size() || levels_[k] >= levels_[k + 1]);
        if (h >= 4.0 && localMax)
            peaks.push_back({ fc[k], h });
    }
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.height > b.height; });
    for (size_t i = 0; i < peaks.size() && i < 2; ++i)
    {
        // Refine the frequency with the full-resolution spectrum (loudest bin within +-1/6 octave).
        double f = peaks[i].f;
        const double binHz = sampleRate_ / (2.0 * static_cast<double>(avgPower_.size() - 1));
        float best = -1.0f;
        for (auto k = static_cast<size_t>(peaks[i].f / 1.12246 / binHz); k < avgPower_.size() && k * binHz <= peaks[i].f * 1.12246; ++k)
            if (avgPower_[k] > best)
                best = avgPower_[k], f = k * binHz;
        const float cut = -static_cast<float>(std::clamp(peaks[i].height - 1.5, 1.5, 6.0));
        EqSuggestion s;
        s.id = "resonance";
        s.band = bellFor(f);
        s.settings = band(static_cast<float>(f), cut, 4.0f);
        s.title = "Cut a resonance at " + hz(f);
        s.why = "A narrow peak sticks out " + db(peaks[i].height) + " above the neighbouring frequencies. It often "
                "comes from " + std::string(drum ? "the drum's ring or the room"
                                         : source == gain::SourceType::Instrument ? "the instrument's body, the room or feedback build-up"
                                                                                  : "the room, the mic or feedback build-up")
                + "; a narrow cut evens it out.";
        offer(s);
    }

    // 3. Mud / boxiness: 200-500 Hz (drums 250-600 Hz) above the overall tilt.
    const double mudLo = drum ? 250.0 : 200.0, mudHi = drum ? 600.0 : 500.0;
    const double mud = meanExcess(mudLo, mudHi)
                       - 0.5 * (meanExcess(mudLo / 2.0, mudLo / 1.25) + meanExcess(mudHi * 1.6, mudHi * 2.5));
    if (mud >= 3.0)
    {
        const float f = drum ? 400.0f : 300.0f;
        EqSuggestion s;
        s.id = "mud";
        s.band = 2;
        s.settings = band(f, -static_cast<float>(std::min(5.0, mud)), drum ? 1.4f : 1.0f);
        s.title = std::string(drum ? "Reduce boxiness" : "Reduce muddiness") + " around " + hz(static_cast<double>(f));
        s.why = "The " + hz(mudLo) + " to " + hz(mudHi) + " range is " + db(mud) + " above its neighbouring frequencies (compared "
                "with a typical " + std::string(gain::words(source).noun) + "). A broad, gentle "
                "cut here usually makes the " + std::string(gain::words(source).noun) + " clearer without thinning it.";
        offer(s);
    }

    // 4. Harshness: 2.5-5 kHz above the tilt (voices and instruments).
    if (!drum)
    {
        const double harsh = meanExcess(2500.0, 5000.0) - 0.5 * (meanExcess(1000.0, 1600.0) + meanExcess(8000.0, 10000.0));
        if (harsh >= 3.0)
        {
            EqSuggestion s;
            s.id = "harsh";
            s.band = 4;
            s.settings = band(3500.0f, -static_cast<float>(std::min(4.0, harsh)), 1.5f);
            s.title = "Soften harshness around 3.5 kHz";
            s.why = "The 2.5 to 5 kHz range is " + db(harsh) + " above its neighbouring frequencies; that is where the ear is "
                    "most sensitive, so it can sound harsh or tiring at volume.";
            offer(s);
        }
    }

    // 5. Air: a vocal that is dull above 10 kHz.
    if (source == gain::SourceType::Vocal && nyq > 14000.0)
    {
        const double air = meanExcess(10000.0, 16000.0) - meanExcess(2000.0, 5000.0);
        if (air <= -8.0)
        {
            EqSuggestion s;
            s.id = "air";
            s.band = 5;
            s.settings = band(10000.0f, 2.5f, 0.707f);
            s.title = "Add a little air above 10 kHz";
            s.why = "The top octave is " + db(air) + " below a typical vocal (relative to its presence range), so it can "
                    "sound dull or distant. A gentle shelf adds presence; skip it if the mic or room is noisy.";
            offer(s);
        }
    }

    for (auto& [b, s] : byBand)
        out.push_back(std::move(s));
    std::sort(out.begin(), out.end(), [](const EqSuggestion& a, const EqSuggestion& b) {
        const auto score = [](const EqSuggestion& s) { return s.id == "hpf-rumble" ? 100.0f : std::abs(s.settings.gainDb); };
        return score(a) > score(b);
    });
    if (out.size() > 5)
        out.resize(5);
    return out;
}

} // namespace mixcore::eq
