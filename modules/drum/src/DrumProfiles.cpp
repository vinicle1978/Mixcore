#include "mixcore/drum/DrumProfiles.h"

#include <array>

namespace mixcore::drum {

namespace {
DrumSettings make(bool on, float amount, float xover, float lengthMs, float tolDb)
{
    DrumSettings s;
    s.debleedEnabled = on;
    s.debleedAmount = amount;
    s.debleedCrossoverHz = xover;
    s.debleedLengthMs = lengthMs;
    s.debleedToleranceDb = tolDb;
    return s;
}

const std::array<DrumProfile, 4> kProfiles = { {
    { "off", "Off", "De-bleed off.", make(false, 50.0f, 5000.0f, 150.0f, 3.0f) },
    { "snare", "Snare - Cymbal Bleed",
      "Keeps the wires and stick attack; pulls down hi-hat and crash that ride over the hit.",
      make(true, 50.0f, 5000.0f, 150.0f, 3.0f) },
    { "toms", "Toms - Cymbal Bleed",
      "Longer allowed ring for tom sustain; lower crossover catches cymbal wash.",
      make(true, 50.0f, 3000.0f, 250.0f, 3.0f) },
    { "kick", "Kick - Click Only",
      "Very short allowed high end: beater click passes, cymbals and snare crack are reduced.",
      make(true, 40.0f, 2500.0f, 40.0f, 4.0f) },
} };
} // namespace

std::span<const DrumProfile> drumProfiles() noexcept { return kProfiles; }

const DrumProfile* findDrumProfile(std::string_view id) noexcept
{
    for (const auto& p : kProfiles)
        if (p.id == id)
            return &p;
    return nullptr;
}

} // namespace mixcore::drum
