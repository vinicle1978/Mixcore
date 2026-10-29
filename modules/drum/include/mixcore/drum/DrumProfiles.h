#pragma once
// Starting-point de-bleed profiles per drum. Starting points, not rules.

#include "mixcore/drum/DrumSettings.h"

#include <span>
#include <string_view>

namespace mixcore::drum {

struct DrumProfile
{
    std::string_view id;
    std::string_view name;
    std::string_view description;
    DrumSettings settings;
};

std::span<const DrumProfile> drumProfiles() noexcept;
const DrumProfile* findDrumProfile(std::string_view id) noexcept;

} // namespace mixcore::drum
