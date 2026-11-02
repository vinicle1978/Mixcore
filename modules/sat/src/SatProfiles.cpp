#include "mixcore/sat/SatProfiles.h"

namespace mixcore::sat {

// Drives are set for peaks around -8 dBFS (a channel Smart Gain has brought to
// -18 dBFS RMS). Harmonics on those peaks: Vocal Warmth ~3 %, Speech Presence
// ~1.5 %, Instrument Warmth ~4 %, Toms ~6 %, Kick and Snare ~10 %, Vocal Grit
// ~20 % (blended 40 %), Parallel Drive ~30 % (blended 25 %).

namespace {
SatProfile make(std::string name, std::string desc, SatType t, float drive, float mix)
{
    SatProfile p;
    p.name = std::move(name);
    p.description = std::move(desc);
    p.settings.enabled = true;
    p.settings.type = t;
    p.settings.driveDb = drive;
    p.settings.mixPercent = mix;
    p.settings.autoGain = true;
    p.settings.validate();
    return p;
}

std::vector<SatProfile> build()
{
    std::vector<SatProfile> v;
    v.push_back(make("Vocal Warmth", "Tape, light drive: rounder, thicker vocal without audible distortion.", SatType::Tape, 4, 100));
    v.push_back(make("Vocal Grit", "Tube, more drive, blended at 40 %: edge and excitement under the clean vocal.", SatType::Tube, 15, 40));
    v.push_back(make("Speech Presence", "Console, very light: a little density so speech cuts through.", SatType::Console, 4, 100));
    v.push_back(make("Kick Weight", "Tube: even harmonics make the low end audible on small speakers.", SatType::Tube, 9, 100));
    v.push_back(make("Snare Crack", "Clip, blended at 60 %: flattens the very top of the hit for a louder, sharper crack.", SatType::Clip, 10, 60));
    v.push_back(make("Toms Body", "Tape: fuller sustain and rounder attack.", SatType::Tape, 8, 100));
    v.push_back(make("Instrument Warmth", "Tape, light: glue and warmth for keys, guitars and bass.", SatType::Tape, 5, 100));
    v.push_back(make("Parallel Drive", "Heavy Tube drive blended at 25 %: density and harmonics while the dry signal keeps the dynamics.",
                     SatType::Tube, 21, 25));
    return v;
}
} // namespace

const std::vector<SatProfile>& satProfiles()
{
    static const std::vector<SatProfile> profiles = build();
    return profiles;
}

const SatProfile* findSatProfile(const std::string& name)
{
    for (const auto& p : satProfiles())
        if (p.name == name)
            return &p;
    return nullptr;
}

} // namespace mixcore::sat
