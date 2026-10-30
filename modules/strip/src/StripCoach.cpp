#include "mixcore/strip/StripCoach.h"

#include <sstream>

namespace mixcore::strip {

std::vector<gain::Advice> evaluateStripAdvice(const gain::SmartGainTelemetry& gainT, const gate::GateTelemetry& gateT,
                                              const ChannelSettings& settings)
{
    auto out = settings.gainOn ? gain::evaluateSmartGainAdvice(gainT, settings.gain, settings.source)
                               : std::vector<gain::Advice> {};
    const auto& w = gain::words(settings.source);
    const bool voice = settings.source == gain::SourceType::Vocal || settings.source == gain::SourceType::Speech;
    if (!gateT.enabled)
        return out;

    using gain::Advice;
    using gain::AdviceSeverity;

    if (gateT.opensLastSecond >= 6)
    {
        std::ostringstream d;
        d << "The gate is opening and closing rapidly (" << gateT.opensLastSecond << " times in the last second).";
        out.push_back({ "gate-chatter", AdviceSeverity::Warning, d.str(),
                        "Chattering sounds like stuttering or crackling on held notes and cymbal wash.",
                        "Check whether the level is hovering around the threshold, for example on sustained notes.",
                        "Increase hysteresis or hold, or lower the threshold slightly.",
                        true, "The gate is acting on the signal; no settings are changed automatically." });
    }

    const bool sourceActive = gainT.activity == gain::ActivityState::Active;
    if (sourceActive && gateT.closedSeconds >= 3.0f && gateT.state == gate::GateState::Closed)
    {
        std::ostringstream d;
        d.setf(std::ios::fixed);
        d.precision(1);
        d << "The " << w.noun << " is active but the gate has stayed closed for " << gateT.closedSeconds
          << " s (detector " << gateT.detectorDb << " dBFS, threshold " << gateT.thresholdDb << " dBFS).";
        out.push_back({ "gate-stuck-closed", AdviceSeverity::Problem, d.str(),
                        voice ? "Words are being cut or turned down." : "Hits or notes are being cut or turned down.",
                        "Check the threshold, and that the key filter (HPF/LPF) suits this source.",
                        "Use Learn threshold while the source plays, or lower the threshold.",
                        true, "The gate is attenuating the source right now." });
    }

    if (voice && gateT.hardGate && settings.gate.rangeDb >= 30.0f && settings.gate.holdMs < 100.0f
        && (settings.gate.rmsDetector || settings.gate.sidechainHpfHz >= 100.0f) && !settings.gate.transientTrigger)
    {
        out.push_back({ "gate-vocal-hard", AdviceSeverity::Info,
                        std::string("A deep, fast-closing gate is set up on a ") + w.noun + " mic.",
                        "Deep gates with short hold cut breaths, word endings and reverb tails.",
                        "Listen to the ends of phrases.",
                        "Use the 'Vocal - Gentle Expander' profile, or raise hold above 150 ms and reduce range.",
                        false, "No automatic change." });
    }
    return out;
}

} // namespace mixcore::strip
