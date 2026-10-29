#pragma once
// Mix Coach for a whole channel: Smart Gain advice plus gate advice that needs
// to see both modules (e.g. "the vocal is active but the gate stays shut").

#include "mixcore/gain/SmartGainCoach.h"
#include "mixcore/gate/GateTelemetry.h"
#include "mixcore/strip/ChannelStrip.h"

#include <vector>

namespace mixcore::strip {

std::vector<gain::Advice> evaluateStripAdvice(const gain::SmartGainTelemetry& gainT, const gate::GateTelemetry& gateT,
                                              const ChannelSettings& settings);

} // namespace mixcore::strip
