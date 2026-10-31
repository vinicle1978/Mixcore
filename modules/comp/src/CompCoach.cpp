#include "mixcore/comp/CompCoach.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mixcore::comp {

namespace {
constexpr float kActiveDb = -55.0f;   // entries quieter than this are pauses
constexpr float kWindowDb = 30.0f;    // keep entries within 30 dB of the loudest
constexpr double kEntrySeconds = 0.01;

float percentile(std::vector<float> v, double p)
{
    if (v.empty())
        return -150.0f;
    const auto k = static_cast<size_t>(std::clamp(p, 0.0, 1.0) * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

std::string db(double v, bool sign = false)
{
    char b[32];
    std::snprintf(b, sizeof b, sign ? "%+.1f dB" : "%.1f dB", v);
    return b;
}

std::string dbfs(double v)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.0f dBFS", v);
    return b;
}

std::string ms(double v)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.0f ms", v);
    return b;
}

std::string ratioText(double r)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.3g:1", r);
    return b;
}

const char* loudParts(gain::SourceType s)
{
    switch (s)
    {
        case gain::SourceType::Speech: return "sentences";
        case gain::SourceType::Drum: return "hits";
        case gain::SourceType::Instrument: return "notes";
        case gain::SourceType::Vocal: break;
    }
    return "phrases";
}

struct SourceTarget
{
    float reductionDb; ///< typical reduction on the loud parts
    float ratio;
};

SourceTarget targetFor(gain::SourceType s)
{
    switch (s)
    {
        case gain::SourceType::Speech: return { 6.0f, 3.5f };
        case gain::SourceType::Drum: return { 6.0f, 4.0f };
        case gain::SourceType::Instrument: return { 3.0f, 2.5f };
        case gain::SourceType::Vocal: break;
    }
    return { 5.0f, 3.0f };
}

float roundHalf(float v) { return std::round(v * 2.0f) * 0.5f; }
} // namespace

float thresholdForReduction(float levelDb, float reductionDb, float ratio, float kneeDb) noexcept
{
    if (reductionDb <= 0.0f || ratio <= 1.0f)
        return 0.0f;
    // gainChangeDb is monotonic in the threshold: bisection.
    float lo = -80.0f, hi = levelDb + kneeDb; // at hi: no reduction; at lo: maximum
    for (int i = 0; i < 60; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        if (-gainChangeDb(levelDb, mid, ratio, kneeDb) > reductionDb)
            lo = mid; // too much reduction: raise threshold
        else
            hi = mid;
    }
    return 0.5f * (lo + hi);
}

void CompListener::start(double seconds, const LevelTap& tap) noexcept
{
    seconds_ = std::clamp(seconds, 1.0, 60.0);
    listening_ = true;
    ready_ = false;
    active_ = 0;
    entries_.clear();
    read_ = tap.written();
}

float CompListener::progress() const noexcept
{
    if (ready_)
        return 1.0f;
    return static_cast<float>(std::min(1.0, active_ * kEntrySeconds / seconds_));
}

void CompListener::pump(const LevelTap& tap)
{
    if (!listening_)
        return;
    const auto w = tap.written();
    if (w > read_ + static_cast<std::uint64_t>(LevelTap::kSize / 2))
        read_ = w - static_cast<std::uint64_t>(LevelTap::kSize / 2); // fell behind: skip ahead
    for (; read_ < w && listening_; ++read_)
        feed(tap.at(read_));
}

void CompListener::feed(LevelEntry e)
{
    if (!listening_)
        return;
    if (!(e.rmsDb > kActiveDb) || !std::isfinite(e.rmsDb) || !std::isfinite(e.peakDb))
        return;
    entries_.push_back(e);
    ++active_;
    if (active_ * kEntrySeconds >= seconds_)
        finish();
}

void CompListener::finish()
{
    listening_ = false;
    if (entries_.empty())
        return;
    std::vector<float> rms;
    rms.reserve(entries_.size());
    for (const auto& e : entries_)
        rms.push_back(e.rmsDb);
    const float top = percentile(rms, 0.99);
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [top](const LevelEntry& e) { return e.rmsDb < top - kWindowDb; }),
                   entries_.end());
    ready_ = !entries_.empty();
}

LevelStats CompListener::stats(Detector detector) const
{
    LevelStats s;
    if (!ready_)
        return s;
    std::vector<float> level, peaks, rms50;
    double acc = 0.0;
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        const auto& e = entries_[i];
        level.push_back(detector == Detector::Peak ? e.peakDb : e.rmsDb);
        peaks.push_back(e.peakDb);
        // 50 ms RMS (5 entries): how much energy follows each peak.
        acc += std::pow(10.0, static_cast<double>(e.rmsDb) / 10.0);
        if (i >= 5)
            acc -= std::pow(10.0, static_cast<double>(entries_[i - 5].rmsDb) / 10.0);
        if (i >= 4)
            rms50.push_back(static_cast<float>(10.0 * std::log10(std::max(acc / 5.0, 1e-15))));
    }
    s.loudDb = percentile(level, 0.9);
    s.typicalDb = percentile(level, 0.5);
    s.quietDb = percentile(level, 0.1);
    // Crest: loud peaks against loud 50 ms RMS. Drums ~10-20 dB, sustained sources less.
    s.crestDb = rms50.empty() ? 0.0f : percentile(peaks, 0.9) - percentile(rms50, 0.9);
    s.entries = static_cast<int>(entries_.size());
    return s;
}

std::vector<CompSuggestion> CompListener::suggestions(gain::SourceType source, const CompSettings& current) const
{
    std::vector<CompSuggestion> out;
    if (!ready_)
        return out;

    const auto st = stats(current.detector);
    const auto parts = loudParts(source);
    const bool drum = source == gain::SourceType::Drum;
    auto target = targetFor(source);
    const float range = st.loudDb - st.quietDb;
    const bool even = !drum && range < 6.0f;
    if (even)
        target = { 2.0f, 2.0f }; // already even: light "glue" only

    // 1. Threshold / ratio / makeup.
    {
        CompSettings p = current;
        p.enabled = true;
        p.ratio = target.ratio;
        p.thresholdDb = std::clamp(roundHalf(thresholdForReduction(st.loudDb, target.reductionDb, p.ratio, p.kneeDb)), -60.0f, 0.0f);
        // Average reduction over the active material = the level lost; makeup gives it back.
        double sum = 0.0;
        std::vector<float> grs;
        for (const auto& e : entries_)
        {
            const float l = current.detector == Detector::Peak ? e.peakDb : e.rmsDb;
            const float g = -gainChangeDb(l, p.thresholdDb, p.ratio, p.kneeDb);
            sum += static_cast<double>(g);
            grs.push_back(g);
        }
        const float avg = static_cast<float>(sum / static_cast<double>(entries_.size()));
        const float quietGr = -gainChangeDb(st.quietDb, p.thresholdDb, p.ratio, p.kneeDb);
        if (!p.autoMakeup)
            p.makeupDb = std::clamp(roundHalf(avg), 0.0f, 24.0f);

        const float nowGr = current.enabled ? -gainChangeDb(st.loudDb, current.thresholdDb, current.ratio, current.kneeDb) : -1.0f;
        CompSuggestion s;
        s.id = "threshold";
        s.settings = p;
        s.base = current;
        char t[96];
        std::snprintf(t, sizeof t, "Threshold %.1f dB at %s", static_cast<double>(p.thresholdDb), ratioText(static_cast<double>(p.ratio)).c_str());
        s.title = t;
        s.why = "Loud " + std::string(parts) + " reach about " + dbfs(static_cast<double>(st.loudDb)) + ", quiet ones about "
                + dbfs(static_cast<double>(st.quietDb)) + " (a " + db(static_cast<double>(range)) + " range). ";
        if (even)
            s.why += "That is already quite even, so this is light compression: about " + db(static_cast<double>(target.reductionDb))
                     + " on the loud " + parts + ", just to hold it together. ";
        else
            s.why += "This reduces the loud " + std::string(parts) + " by about " + db(static_cast<double>(target.reductionDb))
                     + (quietGr < 0.5f ? " and leaves the quiet ones alone" : " and the quiet ones by about " + db(static_cast<double>(quietGr)))
                     + ", a typical amount for a " + gain::words(source).noun + ". ";
        if (p.autoMakeup)
            s.why += "Auto makeup keeps the level about the same.";
        else
            s.why += "Makeup " + db(static_cast<double>(p.makeupDb), true) + " keeps the level about the same.";
        if (current.enabled && nowGr > 10.0f)
            s.why += " Right now the loud " + std::string(parts) + " are reduced by about " + db(static_cast<double>(nowGr))
                     + ", which can sound squashed.";
        else if (current.enabled && nowGr < 0.5f)
            s.why += " Right now the compressor is hardly doing anything: the " + std::string(parts) + " stay below the threshold.";
        out.push_back(std::move(s));
    }

    // 2. Attack: keep drum punch; catch spiky voices.
    {
        CompSettings p = current; // only the attack changes (apply independently of the threshold)
        p.enabled = true;
        CompSuggestion s;
        s.id = "attack";
        if (drum && st.crestDb > 10.0f && current.attackMs < 8.0f)
        {
            p.attackMs = 15.0f;
            s.title = "Slower attack (" + ms(15.0) + ") to keep the punch";
            s.why = "The hits are very transient (peaks " + db(static_cast<double>(st.crestDb)) + " above their average). With "
                    + ms(static_cast<double>(current.attackMs)) + " attack the compressor flattens the front of each hit; "
                    "15 ms lets the stick attack through and controls the body.";
        }
        else if (!drum && st.crestDb > 14.0f && current.attackMs > 15.0f)
        {
            p.attackMs = 5.0f;
            s.title = "Faster attack (" + ms(5.0) + ") for the peaks";
            s.why = std::string("The ") + gain::words(source).noun + " has sharp peaks (" + db(static_cast<double>(st.crestDb))
                    + " above its average, e.g. plosives or picking). A faster attack catches them before they jump out.";
        }
        if (!s.title.empty())
        {
            s.settings = p;
            s.base = current;
            out.push_back(std::move(s));
        }
    }
    return out;
}

} // namespace mixcore::comp
