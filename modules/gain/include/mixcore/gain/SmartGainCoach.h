#pragma once
// Mix Coach rules for the Smart Gain Stage.
//
// Pure function of telemetry + settings -> plain-language advice. Runs on the
// UI/message thread (allocates). Every item answers the five questions from
// the product spec: what was detected, why it matters, what to check, what
// MixCore recommends, and whether anything changes automatically.

#include "mixcore/gain/SmartGainSettings.h"
#include "mixcore/gain/SmartGainTelemetry.h"

#include <string>
#include <vector>

namespace mixcore::gain {

enum class AdviceSeverity
{
    Info,
    Warning,
    Problem
};

struct Advice
{
    std::string id;            ///< Stable identifier, e.g. "input-clipping".
    AdviceSeverity severity = AdviceSeverity::Info;
    std::string detected;
    std::string whyItMatters;
    std::string whatToCheck;
    std::string recommendation;
    bool changesAudioAutomatically = false;
    std::string automaticNote; ///< What (if anything) MixCore is already doing.
};

std::vector<Advice> evaluateSmartGainAdvice(const SmartGainTelemetry& t, const SmartGainSettings& s);

} // namespace mixcore::gain
