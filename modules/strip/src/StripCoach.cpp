#include "mixcore/strip/StripCoach.h"

#include <sstream>

namespace mixcore::strip {

namespace {
void replaceAdvice(std::vector<gain::Advice>& out, const gain::SmartGainTelemetry& gainT, const drum::ReplaceTelemetry& r)
{
    using gain::AdviceSeverity;
    if (!r.enabled)
        return;
    if (!r.kitLoaded)
    {
        out.push_back({ "replace-no-kit", AdviceSeverity::Warning, "Drum replacement is on, but no samples are loaded.",
                        "Hits are detected, but there is nothing to play.",
                        "The kit menu on the Drums page (Replace).",
                        "Choose a starter kit, or load your own WAV samples or a kit folder.",
                        false, "The original drum plays as normal." });
        return;
    }
    if (r.hitsLastSecond >= 14)
    {
        out.push_back({ "replace-double-triggers", AdviceSeverity::Warning,
                        "Replacement is triggering very often (" + std::to_string(r.hitsLastSecond) + " hits in the last second).",
                        "Double triggers or bleed triggers sound like flams or ghost notes that were never played.",
                        "Watch the hit markers in the graph: one marker per real hit?",
                        "Raise the threshold or the retrigger time, or narrow the detector (Low / High) to this drum.",
                        true, "Every detected hit plays a sample." });
    }
    const bool playing = gainT.activity == gain::ActivityState::Active && gainT.inputRmsDb > -50.0f;
    if (playing && r.secondsSinceHit >= 4.0f && r.detectorDb < r.thresholdDb && r.detectorDb > -149.0f)
    {
        out.push_back({ "replace-no-hits", AdviceSeverity::Warning,
                        "The drum is playing but no hits have been detected for a while.",
                        "With the original turned down, missed hits mean missing drum hits in the mix.",
                        "Compare the detector level (blue) with the threshold line in the Replace graph.",
                        "Lower the threshold until every real hit shows a marker.",
                        false, "No automatic change." });
    }
}
} // namespace

std::vector<gain::Advice> evaluateStripAdvice(const gain::SmartGainTelemetry& gainT, const gate::GateTelemetry& gateT,
                                              const ChannelSettings& settings, const drum::ReplaceTelemetry* replaceT)
{
    auto out = settings.gainOn ? gain::evaluateSmartGainAdvice(gainT, settings.gain, settings.source)
                               : std::vector<gain::Advice> {};
    if (replaceT != nullptr)
        replaceAdvice(out, gainT, *replaceT);
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
