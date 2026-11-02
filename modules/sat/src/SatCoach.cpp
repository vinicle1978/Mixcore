#include "mixcore/sat/SatCoach.h"

#include "mixcore/sat/SatCurves.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

namespace mixcore::sat {

namespace {
std::string fmt(const char* f, double v)
{
    char b[48];
    std::snprintf(b, sizeof b, f, v);
    return b;
}
std::string percent(float db) { return fmt("%.1f %%", 100.0 * std::pow(10.0, static_cast<double>(db) / 20.0)); }
std::string lower(const char* s)
{
    std::string r(s);
    for (auto& c : r)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
} // namespace

float targetThdDb(gain::SourceType source) noexcept
{
    switch (source)
    {
        case gain::SourceType::Speech: return -38.0f;
        case gain::SourceType::Drum: return -22.0f;
        case gain::SourceType::Instrument: return -28.0f;
        case gain::SourceType::Vocal: break;
    }
    return -32.0f;
}

std::vector<SatSuggestion> satSuggestions(const comp::LevelStats& stats, gain::SourceType source, const SatSettings& current)
{
    std::vector<SatSuggestion> out;
    if (stats.entries <= 0)
        return out;
    const auto& w = gain::words(source);
    const bool voice = source == gain::SourceType::Vocal || source == gain::SourceType::Speech;
    const float target = targetThdDb(source);

    // 1. Type: a hard clipper is rarely what a voice wants.
    SatType type = current.type;
    if (voice && current.type == SatType::Clip)
    {
        SatSuggestion s;
        s.id = "type";
        s.base = current;
        s.settings = current;
        s.settings.enabled = true;
        s.settings.type = SatType::Tape;
        s.title = "Use Tape instead of Clip";
        s.why = "Clip flattens the peaks hard and sounds harsh on a " + std::string(w.noun)
                + ". Tape rounds them off smoothly, so the same drive sounds warmer and less distorted.";
        out.push_back(s);
        type = SatType::Tape;
    }

    // 2. Drive for a typical amount of harmonics on the loud peaks.
    {
        const float drive = std::round(driveForThd(type, stats.loudDb, target) * 2.0f) * 0.5f;
        const float got = thdDb(type, stats.loudDb, drive);
        SatSuggestion s;
        s.id = "drive";
        s.base = current;
        s.settings = current;
        s.settings.enabled = true;
        s.settings.driveDb = drive;
        s.settings.autoGain = true;
        s.title = fmt("Drive %.1f dB", static_cast<double>(drive)) + " (" + satTypeName(type) + ")";
        s.why = "Loud peaks reach about " + fmt("%.0f dBFS", static_cast<double>(stats.loudDb)) + ". With this drive the "
                + lower(satTypeName(type)) + " curve adds harmonics about " + fmt("%.0f dB", static_cast<double>(-got)) + " below the "
                + w.noun + " (" + percent(got) + ") on those peaks: "
                + (source == gain::SourceType::Drum ? "audible colour and weight, typical for a drum"
                   : voice                         ? "warmth without obvious distortion, typical for a " + std::string(w.noun)
                                                   : "gentle colour, typical for an instrument")
                + ". Auto gain keeps the level the same, so you hear the colour, not just louder.";
        if (current.enabled)
        {
            const float now = thdDb(current.type, stats.loudDb, current.driveDb);
            if (now > target + 10.0f)
                s.why += " Right now the harmonics are at about " + percent(now) + ", which can sound distorted or harsh.";
            else if (now < target - 15.0f)
                s.why += " Right now the effect is very subtle (" + percent(now) + ").";
        }
        if (drive <= 0.05f)
            s.why = "Loud peaks reach about " + fmt("%.0f dBFS", static_cast<double>(stats.loudDb)) + ", so the "
                    + lower(satTypeName(type)) + " curve already adds harmonics about " + fmt("%.0f dB", static_cast<double>(-got))
                    + " below the " + w.noun + " (" + percent(got) + ") with no extra drive. Keep Drive at 0, or lower the level "
                    "into the saturation (static gain, compressor makeup) for a cleaner sound."
                    + (current.enabled && current.driveDb > 0.05f ? " Right now Drive is " + fmt("%.1f dB", static_cast<double>(current.driveDb)) + "." : "");
        if (drive >= 35.9f)
            s.why += " Even full drive stays below the target here: the source is quiet at this point; check the gain staging.";
        out.push_back(s);

        // 3. Quality: heavy saturation in Live mode aliases on bright sources.
        const float now = current.enabled ? thdDb(current.type, stats.loudDb, current.driveDb) : -150.0f;
        if (current.quality == SatQuality::Live && std::max(now, got) > -24.0f)
        {
            SatSuggestion q;
            q.id = "quality";
            q.base = current;
            q.settings = current;
            q.settings.enabled = true;
            q.settings.quality = SatQuality::Studio;
            q.title = "Studio quality for heavy drive";
            q.why = "At this much saturation Live mode can add aliasing (harsh, unrelated tones) on bright sounds. Studio runs the "
                    "curve 4x oversampled and is cleaner, but adds 28 samples (about 0.6 ms at 48 kHz) of latency.";
            out.push_back(q);
        }
    }
    return out;
}

} // namespace mixcore::sat
