#pragma once
// Built-in starter kits: original drum sounds synthesised by MixCore (no
// third-party samples), 3 velocity layers x 2 round-robin variations each.
// Louder layers are brighter as well as louder, like a real drum.

#include "mixcore/drum/SampleKit.h"

#include <memory>
#include <string>
#include <vector>

namespace mixcore::drum {

struct StarterKitInfo
{
    const char* id;   ///< "starter:kick"
    const char* name; ///< "Starter Kick"
};

const std::vector<StarterKitInfo>& starterKits();

/// Builds a starter kit (message thread; allocates). nullptr for an unknown id.
std::unique_ptr<SampleKit> makeStarterKit(const std::string& id, double sampleRate = 48000.0);

} // namespace mixcore::drum
