#pragma once
// Channel presets = Smart Gain preset + gate profile + de-bleed profile.
//
// Snare and Toms presets switch de-bleed on (Amount 50 %); every other preset
// leaves it off.
//
// Drum presets switch the gate ON (the main reason to use them). Vocal and
// speech presets load the matching gate profile but leave the gate OFF: a gate
// on a vocal must be fitted to the room (Learn threshold) before it is used,
// and an unfitted gate can cut words.

#include "mixcore/strip/ChannelStrip.h"

#include <string>
#include <vector>

namespace mixcore::strip {

struct ChannelPreset
{
    std::string name;
    std::string category;
    std::string description;
    std::string gateProfile; ///< Name of the gate profile used.
    std::string kit;         ///< Starter kit for drum replacement ("" = keep the current kit).
    ChannelSettings settings;
};

const std::vector<ChannelPreset>& channelPresets();
const ChannelPreset* findChannelPreset(const std::string& name);

/// Everything from the preset except session values: static gain and bypass.
ChannelSettings applyChannelPreset(const ChannelSettings& current, const ChannelSettings& preset);

/// True when `current` matches `preset` on every preset-controlled field.
bool matchesChannelPreset(const ChannelSettings& current, const ChannelSettings& preset, float tolerance = 1.0e-3f);

bool drumSettingsMatch(const drum::DrumSettings& a, const drum::DrumSettings& b, float tolerance = 1.0e-3f);

/// Text form used by user preset files and tests.
std::string serialiseGate(const ChannelSettings& s);

} // namespace mixcore::strip
