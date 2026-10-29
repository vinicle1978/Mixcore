#pragma once
// Source profiles for the gate. Each is a complete GateSettings tuned for one
// kind of source. Thresholds assume the channel has been calibrated with the
// Smart Gain Stage first (vocals around -18 dBFS RMS, drums around -22); use
// "Learn threshold" to fit the actual room.

#include "mixcore/gate/GateSettings.h"

#include <string>
#include <vector>

namespace mixcore::gate {

struct GateProfile
{
    std::string name;
    std::string description;
    GateSettings settings; ///< enabled == true
};

const std::vector<GateProfile>& gateProfiles();
const GateProfile* findGateProfile(const std::string& name);

/// Float-tolerant comparison of every gate field.
bool gateSettingsMatch(const GateSettings& a, const GateSettings& b, float tolerance = 1.0e-3f);

} // namespace mixcore::gate
