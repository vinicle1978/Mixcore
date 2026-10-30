#include "mixcore/strip/ChannelPresets.h"

#include "mixcore/gain/SmartGainPresets.h"
#include "mixcore/drum/DrumProfiles.h"
#include "mixcore/gate/GateProfiles.h"

#include <cmath>

#include <utility>

namespace mixcore::strip {

namespace {

// Smart Gain preset -> (gate profile, gate on?)
const std::pair<const char*, std::pair<const char*, bool>> kGateFor[] = {
    { "Lead Vocal - Natural", { "Vocal - Gentle Expander", false } },
    { "Lead Vocal - Controlled", { "Vocal - Gentle Expander", false } },
    { "Lead Vocal - Dynamic Singer", { "Vocal - Gentle Expander", false } },
    { "Lead Vocal - Live Stream", { "Vocal - Gentle Expander", false } },
    { "Backing Vocal - Airy", { "Backing Vocal", false } },
    { "Backing Vocal - Tight", { "Backing Vocal", false } },
    { "Backing Vocal - Choir / Group", { "Backing Vocal", false } },
    { "Speech - Pastor / Host", { "Speech", false } },
    { "Speech - Stream / Podcast", { "Speech", false } },
    { "Kick Drum", { "Kick", true } },
    { "Snare Drum", { "Snare", true } },
    { "Toms", { "Toms", true } },
};

// Smart Gain preset -> de-bleed profile (on). Others get de-bleed off.
// Smart Gain preset -> starter kit loaded for replacement (replacement stays off).
const std::pair<const char*, const char*> kKitFor[] = {
    { "Kick Drum", "starter:kick" },
    { "Snare Drum", "starter:snare" },
    { "Toms", "starter:tom" },
};

const std::pair<const char*, const char*> kDebleedFor[] = {
    { "Snare Drum", "snare" },
    { "Toms", "toms" },
};

std::vector<ChannelPreset> build()
{
    std::vector<ChannelPreset> out;
    for (const auto& g : gain::factoryPresets())
    {
        ChannelPreset p;
        p.name = g.name;
        p.category = g.category;
        p.description = g.description;
        p.settings.gain = g.settings;
        p.settings.gainOn = true;
        p.settings.source = g.category == "Drums" ? gain::SourceType::Drum
                            : g.category == "Speech" ? gain::SourceType::Speech
                                                     : gain::SourceType::Vocal;
        for (const auto& [gainName, gateChoice] : kGateFor)
        {
            if (g.name == gainName)
            {
                if (const auto* profile = gate::findGateProfile(gateChoice.first))
                {
                    p.gateProfile = profile->name;
                    p.settings.gate = profile->settings;
                    p.settings.gate.enabled = gateChoice.second;
                    p.description += gateChoice.second ? " Gate: " + profile->name + " (on)."
                                                       : " Gate: " + profile->name + " (off - turn on after Learn threshold).";
                }
            }
        }
        for (const auto& [gainName, profileId] : kDebleedFor)
            if (g.name == gainName)
                if (const auto* profile = drum::findDrumProfile(profileId))
                {
                    p.settings.drum = profile->settings;
                    p.description += " De-bleed: on (" + std::string(profile->name) + ").";
                }
        for (const auto& [gainName, kitId] : kKitFor)
            if (g.name == gainName)
                p.kit = kitId;
        // Replacement detector tuned per drum (replacement itself stays off).
        if (g.name == "Kick Drum")
        {
            p.settings.drum.replaceDetectHighHz = 200.0f; // ignore snare and cymbals
            p.settings.drum.replaceThresholdDb = -26.0f;
        }
        else if (g.name == "Snare Drum")
        {
            p.settings.drum.replaceDetectLowHz = 150.0f; // ignore the kick
            p.settings.drum.replaceThresholdDb = -26.0f;
        }
        else if (g.name == "Toms")
        {
            p.settings.drum.replaceDetectLowHz = 60.0f;
            p.settings.drum.replaceDetectHighHz = 2000.0f; // ignore cymbals
            p.settings.drum.replaceThresholdDb = -28.0f;
            p.settings.drum.replaceRetriggerMs = 60.0f;
        }
        p.settings.validate();
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace

const std::vector<ChannelPreset>& channelPresets()
{
    static const std::vector<ChannelPreset> presets = build();
    return presets;
}

const ChannelPreset* findChannelPreset(const std::string& name)
{
    for (const auto& p : channelPresets())
        if (p.name == name)
            return &p;
    return nullptr;
}

bool drumSettingsMatch(const drum::DrumSettings& a, const drum::DrumSettings& b, float tolerance)
{
    return drum::approximatelyEqual(a, b, tolerance);
}

ChannelSettings applyChannelPreset(const ChannelSettings& current, const ChannelSettings& preset)
{
    ChannelSettings s;
    s.gain = gain::applyPreset(current.gain, preset.gain);
    s.gainOn = preset.gainOn;
    s.source = preset.source;
    s.gate = preset.gate;
    s.drum = preset.drum;
    s.validate();
    return s;
}

bool matchesChannelPreset(const ChannelSettings& current, const ChannelSettings& preset, float tolerance)
{
    return current.gainOn == preset.gainOn && current.source == preset.source
           && gain::matchesPreset(current.gain, preset.gain, tolerance)
           && gate::gateSettingsMatch(current.gate, preset.gate, tolerance)
           && drumSettingsMatch(current.drum, preset.drum, tolerance);
}

std::string serialiseGate(const ChannelSettings& s) { return s.gate.serialise(); }

} // namespace mixcore::strip
