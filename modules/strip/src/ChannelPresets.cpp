#include "mixcore/strip/ChannelPresets.h"

#include "mixcore/gain/SmartGainPresets.h"
#include "mixcore/gate/GateProfiles.h"

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

ChannelSettings applyChannelPreset(const ChannelSettings& current, const ChannelSettings& preset)
{
    ChannelSettings s;
    s.gain = gain::applyPreset(current.gain, preset.gain);
    s.gate = preset.gate;
    s.validate();
    return s;
}

bool matchesChannelPreset(const ChannelSettings& current, const ChannelSettings& preset, float tolerance)
{
    return gain::matchesPreset(current.gain, preset.gain, tolerance)
           && gate::gateSettingsMatch(current.gate, preset.gate, tolerance);
}

std::string serialiseGate(const ChannelSettings& s) { return s.gate.serialise(); }

} // namespace mixcore::strip
