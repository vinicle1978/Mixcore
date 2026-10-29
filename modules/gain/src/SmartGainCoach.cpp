#include "mixcore/gain/SmartGainCoach.h"

#include <cmath>
#include <sstream>

namespace mixcore::gain {

namespace {
std::string db(float v)
{
    std::ostringstream s;
    s.setf(std::ios::fixed);
    s.precision(1);
    s << (v > 0.0f ? "+" : "") << v << " dB";
    return s.str();
}
} // namespace

std::vector<Advice> evaluateSmartGainAdvice(const SmartGainTelemetry& t, const SmartGainSettings& s)
{
    std::vector<Advice> out;

    if (t.inputClipping)
    {
        out.push_back({ "input-clipping", AdviceSeverity::Problem,
                        "The input signal is hitting full scale before MixCore processes it.",
                        "Clipping at the interface is permanent distortion. No plugin, including MixCore, can repair it.",
                        "Check the preamp gain on your interface or mixer, and the microphone distance during loud passages.",
                        "Lower the preamp gain until the loudest moments peak well below 0 dBFS, then recalibrate.",
                        false, "MixCore does not change the input; this needs a hardware adjustment." });
    }

    if (t.nonFiniteInputSamples > 0)
    {
        out.push_back({ "invalid-input", AdviceSeverity::Problem,
                        "Invalid (NaN/infinite) samples arrived at the input.",
                        "This usually means an upstream plugin or driver fault and can cause loud bursts.",
                        "Check the plugins before MixCore and your audio driver.",
                        "Remove or update the faulty upstream component.",
                        true, "MixCore replaces invalid samples with silence to protect your speakers." });
    }

    const bool active = t.activity == ActivityState::Active || t.activity == ActivityState::Hold;

    if (active && t.inputRmsDb < -42.0f && t.inputRmsDb > -100.0f)
    {
        out.push_back({ "input-quiet", AdviceSeverity::Warning,
                        "The vocal input is unusually quiet (" + db(t.inputRmsDb) + "FS RMS).",
                        "Large amounts of digital gain also raise the room noise and bleed by the same amount.",
                        "Check the preamp gain before adding large amounts of digital gain.",
                        "Raise the preamp gain, then run calibration again.",
                        false, "No automatic change." });
    }

    if (t.peakGuardActive)
    {
        out.push_back({ "peak-guard-active", AdviceSeverity::Warning,
                        "The peak guard is reducing gain by up to " + db(t.peakGuardReductionDb) + ".",
                        "The guard is a safety net. Frequent action means the level going into it is too hot.",
                        "Review the upstream signal level and the static gain before increasing the target.",
                        "Reduce static gain or the target level until the guard only acts occasionally.",
                        true, "The guard is already preventing peaks above the ceiling." });
    }
    else if (t.peakWarning)
    {
        out.push_back({ "peak-warning", AdviceSeverity::Warning,
                        "Output peaks are above the " + db(s.peakWarningDb) + "FS warning level.",
                        "Leaving headroom here protects the mix bus and later processing from overload.",
                        "Check the static gain and whether the singer gets much louder in some sections.",
                        "Lower the static gain slightly, or enable the peak guard if occasional peaks are expected.",
                        false, "No automatic change." });
    }

    if (t.peakRestrictionActive)
    {
        out.push_back({ "rider-restricted", AdviceSeverity::Info,
                        "The rider wants " + db(t.riderRestrictionDb) + " more gain but peak safety is holding it back.",
                        "The vocal has loud peaks relative to its average level (high crest factor).",
                        "Check for plosives or mic handling noise; a high-pass filter or pop filter often helps.",
                        "Accept the restriction, or reduce peaks upstream before raising the target.",
                        true, "The rider is automatically limiting its own upward gain." });
    }

    if (t.riderEnabled && t.targetErrorValid && t.targetErrorDb > 3.0f
        && t.riderGainDb >= std::min(s.riderMaxUpDb, s.riderRangeDb) - 0.25f)
    {
        out.push_back({ "below-target", AdviceSeverity::Info,
                        "The vocal is active, but its average level remains " + db(t.targetErrorDb) + " below the target.",
                        "The rider is already at its maximum upward correction, so it cannot close the gap.",
                        "Check that static gain has been calibrated for this singer.",
                        "Run calibration, or raise static gain manually.",
                        false, "The rider is at its limit; static gain is never changed automatically." });
    }

    if (!active && t.noiseFloorDb > -50.0f && t.noiseFloorDb > -149.0f)
    {
        out.push_back({ "high-noise-floor", AdviceSeverity::Warning,
                        "The background level between phrases is high (" + db(t.noiseFloorDb) + "FS).",
                        "A high noise floor makes activity detection less reliable and adds noise to the mix.",
                        "Check for monitor spill, fans, or a gain setting that is far too high.",
                        "Reduce spill or noise at the source; consider a gate after MixCore.",
                        false, "The rider freezes while no vocal is detected, so it will not boost the noise." });
    }

    if (t.calibration == CalibrationState::Collecting)
    {
        out.push_back({ "calibrating", AdviceSeverity::Info,
                        "Calibration is listening for active vocal.",
                        "Static gain is set from a representative performance level, not a single peak.",
                        "Sing or speak at the level you will use in the performance.",
                        "Keep going until the progress bar is full.",
                        false, "Nothing is applied until you confirm the result." });
    }

    return out;
}

} // namespace mixcore::gain
