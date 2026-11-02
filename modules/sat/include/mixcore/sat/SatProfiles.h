#pragma once
// Starting-point saturation settings per source.

#include "mixcore/sat/SatSettings.h"

#include <string>
#include <vector>

namespace mixcore::sat {

struct SatProfile
{
    std::string name;
    std::string description;
    SatSettings settings; ///< enabled == true
};

const std::vector<SatProfile>& satProfiles();
const SatProfile* findSatProfile(const std::string& name);

} // namespace mixcore::sat
