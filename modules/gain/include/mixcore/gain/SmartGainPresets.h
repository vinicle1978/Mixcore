#pragma once
// Factory presets for the Smart Gain Stage.
//
// A preset is a complete SmartGainSettings, but applying one never changes the
// session-specific values: static gain (set by calibration for *this* singer /
// mic / preamp) and bypass. See applyPreset().
//
// All values are starting points, chosen for typical live sources. They are
// not universal standards and should be refined in listening tests.

#include "mixcore/gain/SmartGainSettings.h"

#include <string>
#include <vector>

namespace mixcore::gain {

struct FactoryPreset
{
    std::string name;        ///< Unique, shown in menus and host program lists.
    std::string category;    ///< Menu section, e.g. "Lead Vocal".
    std::string description; ///< One or two sentences for the UI tooltip.
    SmartGainSettings settings;
};

/// The factory preset list, in menu order. The first entry is the default.
const std::vector<FactoryPreset>& factoryPresets();

/// Returns nullptr when no factory preset has that name.
const FactoryPreset* findFactoryPreset(const std::string& name);

/// Settings that result from applying `preset` to a session whose current
/// settings are `current`: everything comes from the preset except static gain
/// and bypass, which are kept. The result is validated.
SmartGainSettings applyPreset(const SmartGainSettings& current, const SmartGainSettings& preset);

/// True when `current` matches `preset` on every field a preset controls
/// (i.e. ignoring static gain and bypass), within `tolerance` for floats.
bool matchesPreset(const SmartGainSettings& current, const SmartGainSettings& preset, float tolerance = 1.0e-3f);

} // namespace mixcore::gain
