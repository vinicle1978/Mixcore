#pragma once
// Starting-point EQ curves per source. Starting points, not rules: every
// source, mic and room is different (use Listen & suggest, then your ears).

#include "mixcore/eq/EqSettings.h"

#include <string>
#include <vector>

namespace mixcore::eq {

struct EqProfile
{
    std::string name;
    std::string description;
    EqSettings settings; ///< enabled == true
};

const std::vector<EqProfile>& eqProfiles();
const EqProfile* findEqProfile(const std::string& name);

} // namespace mixcore::eq
