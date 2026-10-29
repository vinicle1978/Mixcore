#include "Parameters.h"

namespace mixcore::plugin {

using S = gain::SmartGainSettings;

const std::vector<FloatParamSpec>& floatParamSpecs()
{
    static const std::vector<FloatParamSpec> specs {
        { ids::targetRms, "Target RMS", &S::targetRmsDb, -40.0f, -6.0f, 0.1f, 0.0f, "dBFS", 1, false },
        { ids::staticGain, "Static Gain", &S::staticGainDb, -18.0f, 18.0f, 0.1f, 0.0f, "dB", 1, false },
        { ids::riderStrength, "Rider Strength", &S::riderStrength, 0.0f, 1.0f, 0.01f, 0.0f, "", 2, false },
        { ids::riderRange, "Rider Range", &S::riderRangeDb, 0.0f, 12.0f, 0.1f, 0.0f, "dB", 1, true },
        { ids::riderMaxUp, "Rider Max Up", &S::riderMaxUpDb, 0.0f, 12.0f, 0.1f, 0.0f, "dB", 1, true },
        { ids::riderMaxDown, "Rider Max Down", &S::riderMaxDownDb, 0.0f, 12.0f, 0.1f, 0.0f, "dB", 1, true },
        { ids::riderAttack, "Rider Attack", &S::riderAttackMs, 10.0f, 2000.0f, 1.0f, 200.0f, "ms", 0, true },
        { ids::riderRelease, "Rider Release", &S::riderReleaseMs, 10.0f, 5000.0f, 1.0f, 600.0f, "ms", 0, true },
        { ids::riderMaxRate, "Rider Max Rate", &S::riderMaxRateDbPerSec, 0.5f, 60.0f, 0.1f, 8.0f, "dB/s", 1, true },
        { ids::deadband, "Deadband", &S::deadbandDb, 0.0f, 6.0f, 0.1f, 0.0f, "dB", 1, true },
        { ids::idleDelay, "Idle Release Delay", &S::idleReleaseDelayMs, 250.0f, 10000.0f, 10.0f, 2000.0f, "ms", 0, true },
        { ids::idleRate, "Idle Release Rate", &S::idleReleaseDbPerSec, 0.0f, 6.0f, 0.1f, 0.0f, "dB/s", 1, true },
        { ids::activityConfirm, "Activity Confirm", &S::activityConfirmMs, 0.0f, 500.0f, 1.0f, 0.0f, "ms", 0, true },
        { ids::phraseHold, "Phrase Hold", &S::phraseHoldMs, 0.0f, 2000.0f, 1.0f, 300.0f, "ms", 0, true },
        { ids::activityMargin, "Activity Margin", &S::activityMarginDb, 3.0f, 30.0f, 0.5f, 0.0f, "dB", 1, true },
        { ids::peakWarning, "Peak Warning", &S::peakWarningDb, -24.0f, 0.0f, 0.1f, 0.0f, "dBFS", 1, true },
        { ids::peakRestrict, "Peak Restrict", &S::peakRestrictDb, -24.0f, 0.0f, 0.1f, 0.0f, "dBFS", 1, true },
        { ids::guardCeiling, "Guard Ceiling", &S::peakGuardCeilingDb, -24.0f, 0.0f, 0.1f, 0.0f, "dBFS", 1, true },
    };
    return specs;
}

const std::vector<BoolParamSpec>& boolParamSpecs()
{
    static const std::vector<BoolParamSpec> specs {
        { ids::riderEnabled, "Rider On", &S::riderEnabled, true },
        // Toggling the guard changes plugin latency; hosts handle latency
        // changes poorly under automation, so it is a setting, not automation.
        { ids::peakGuard, "Peak Guard", &S::peakGuardEnabled, false },
        { ids::bypass, "Bypass", &S::bypass, true },
    };
    return specs;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const S defaults;

    for (const auto& p : floatParamSpecs())
    {
        juce::NormalisableRange<float> range(p.min, p.max, p.step);
        // Skew only when the centre lies strictly inside the range; 0 means
        // "linear" even for ranges that do not contain 0 (e.g. -40..-6 dBFS).
        if (p.skewCentre > p.min && p.skewCentre < p.max && std::abs(p.skewCentre) > 0.0f)
            range.setSkewForCentre(p.skewCentre);

        const int decimals = p.decimals;
        const juce::String unit(p.unit);
        auto attributes = juce::AudioParameterFloatAttributes()
                              .withLabel(unit)
                              .withStringFromValueFunction([decimals, unit](float v, int) {
                                  return juce::String(v, decimals) + (unit.isEmpty() ? "" : " " + unit);
                              })
                              .withValueFromStringFunction([](const juce::String& t) { return t.getFloatValue(); });

        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { p.id, 1 }, p.name, range,
                                                               defaults.*p.member, attributes));
    }

    for (const auto& b : boolParamSpecs())
    {
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { b.id, 1 }, b.name, defaults.*b.member,
            juce::AudioParameterBoolAttributes().withAutomatable(b.automatable)));
    }
    return layout;
}

ParameterReader::ParameterReader(juce::AudioProcessorValueTreeState& state)
{
    for (const auto& p : floatParamSpecs())
    {
        auto* raw = state.getRawParameterValue(p.id);
        jassert(raw != nullptr);
        floats_.emplace_back(raw, p.member);
    }
    for (const auto& b : boolParamSpecs())
    {
        auto* raw = state.getRawParameterValue(b.id);
        jassert(raw != nullptr);
        bools_.emplace_back(raw, b.member);
    }
}

gain::SmartGainSettings ParameterReader::read() const noexcept
{
    S s;
    for (const auto& [raw, member] : floats_)
        s.*member = raw->load(std::memory_order_relaxed);
    for (const auto& [raw, member] : bools_)
        s.*member = raw->load(std::memory_order_relaxed) >= 0.5f;
    return s; // the engine validates ranges again in setSettings()
}

} // namespace mixcore::plugin
