#pragma once
// Starting-point compressor settings per source. Thresholds assume Smart Gain
// has brought the channel near its target (-18 dBFS RMS by default).

#include "mixcore/comp/CompSettings.h"

#include <string>
#include <vector>

namespace mixcore::comp {

struct CompProfile
{
    std::string name;
    std::string description;
    CompSettings settings; ///< enabled == true
};

const std::vector<CompProfile>& compProfiles();
const CompProfile* findCompProfile(const std::string& name);

} // namespace mixcore::comp
