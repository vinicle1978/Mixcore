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
        { ids::detectorHpf, "Detector HPF", &S::detectorHpfHz, 20.0f, 400.0f, 1.0f, 100.0f, "Hz", 0, true },
        { ids::peakWarning, "Peak Warning", &S::peakWarningDb, -24.0f, 0.0f, 0.1f, 0.0f, "dBFS", 1, true },
        { ids::peakRestrict, "Peak Restrict", &S::peakRestrictDb, -24.0f, 0.0f, 0.1f, 0.0f, "dBFS", 1, true },
        { ids::guardCeiling, "Guard Ceiling", &S::peakGuardCeilingDb, -24.0f, 0.0f, 0.1f, 0.0f, "dBFS", 1, true },
    };
    return specs;
}

using G = gate::GateSettings;

const std::vector<GateFloatParamSpec>& gateFloatParamSpecs()
{
    static const std::vector<GateFloatParamSpec> specs {
        { ids::gateThreshold, "Gate Threshold", &G::thresholdDb, -80.0f, 0.0f, 0.1f, 0.0f, "dBFS", 1, false },
        { ids::gateRange, "Gate Range", &G::rangeDb, 0.0f, 80.0f, 0.5f, 0.0f, "dB", 1, false },
        { ids::gateAttack, "Gate Attack", &G::attackMs, 0.05f, 50.0f, 0.01f, 2.0f, "ms", 2, false },
        { ids::gateHold, "Gate Hold", &G::holdMs, 0.0f, 2000.0f, 1.0f, 200.0f, "ms", 0, false },
        { ids::gateRelease, "Gate Release", &G::releaseMs, 5.0f, 4000.0f, 1.0f, 300.0f, "ms", 0, false },
        { ids::gateRatio, "Gate Ratio", &G::ratio, 1.2f, gate::kHardGateRatio, 0.1f, 4.0f, ":1", 1, true },
        { ids::gateHysteresis, "Gate Hysteresis", &G::hysteresisDb, 0.0f, 20.0f, 0.1f, 0.0f, "dB", 1, true },
        { ids::gateScHpf, "Gate Key HPF", &G::sidechainHpfHz, 20.0f, 2000.0f, 1.0f, 200.0f, "Hz", 0, true },
        { ids::gateScLpf, "Gate Key LPF", &G::sidechainLpfHz, 500.0f, 20000.0f, 10.0f, 4000.0f, "Hz", 0, true },
        { ids::gateTransientDb, "Gate Transient", &G::transientDb, 1.0f, 24.0f, 0.1f, 0.0f, "dB", 1, true },
        // Lookahead changes plugin latency: a setting, not automation.
        { ids::gateLookahead, "Gate Lookahead", &G::lookaheadMs, 0.0f, 5.0f, 0.1f, 0.0f, "ms", 1, true, false },
    };
    return specs;
}

const std::vector<GateBoolParamSpec>& gateBoolParamSpecs()
{
    static const std::vector<GateBoolParamSpec> specs {
        { ids::gateEnabled, "Gate On", &G::enabled, true },
        { ids::gateRms, "Gate RMS Detector", &G::rmsDetector, true },
        { ids::gateTransient, "Gate Transient Trigger", &G::transientTrigger, true },
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

using D = drum::DrumSettings;

const std::vector<DrumFloatParamSpec>& drumFloatParamSpecs()
{
    static const std::vector<DrumFloatParamSpec> specs {
        { ids::drumDebleedAmount, "De-bleed Amount", &D::debleedAmount, 0.0f, 100.0f, 1.0f, 0.0f, "%", 0, false },
        { ids::drumDebleedCrossover, "De-bleed Crossover", &D::debleedCrossoverHz, 1000.0f, 12000.0f, 10.0f, 4000.0f, "Hz", 0, false },
        { ids::drumDebleedLength, "De-bleed Length", &D::debleedLengthMs, 10.0f, 1000.0f, 1.0f, 150.0f, "ms", 0, false },
        { ids::drumDebleedTolerance, "De-bleed Tolerance", &D::debleedToleranceDb, 0.0f, 12.0f, 0.1f, 0.0f, "dB", 1, false },
        { ids::replaceThreshold, "Replace Threshold", &D::replaceThresholdDb, -60.0f, 0.0f, 0.1f, 0.0f, "dBFS", 1, false },
        { ids::replaceRetrigger, "Replace Retrigger", &D::replaceRetriggerMs, 5.0f, 500.0f, 1.0f, 60.0f, "ms", 0, false },
        { ids::replaceDynamics, "Replace Dynamics", &D::replaceDynamics, 0.0f, 100.0f, 1.0f, 0.0f, "%", 0, false },
        { ids::replaceRange, "Replace Range", &D::replaceRangeDb, 6.0f, 48.0f, 0.5f, 0.0f, "dB", 1, true },
        { ids::replaceMix, "Replace Mix", &D::replaceMix, 0.0f, 100.0f, 0.1f, 0.0f, "%", 1, false },
        { ids::replaceSample, "Replace Sample Level", &D::replaceSampleDb, -40.0f, 12.0f, 0.1f, 0.0f, "dB", 1, false },
        { ids::replaceOriginal, "Replace Original Level", &D::replaceOriginalDb, -60.0f, 12.0f, 0.1f, 0.0f, "dB", 1, false },
        { ids::replacePitch, "Replace Pitch", &D::replacePitchSt, -12.0f, 12.0f, 0.1f, 0.0f, "st", 1, false },
        { ids::replaceAttack, "Replace Attack", &D::replaceAttackMs, 0.0f, 30.0f, 0.1f, 5.0f, "ms", 1, false },
        { ids::replaceLength, "Replace Length", &D::replaceLengthMs, 20.0f, 2000.0f, 1.0f, 300.0f, "ms", 0, false },
        { ids::replaceDetectLow, "Replace Detect Low", &D::replaceDetectLowHz, 20.0f, 2000.0f, 1.0f, 150.0f, "Hz", 0, true },
        { ids::replaceDetectHigh, "Replace Detect High", &D::replaceDetectHighHz, 100.0f, 20000.0f, 10.0f, 2000.0f, "Hz", 0, true },
        // Lookahead changes plugin latency: a setting, not automation.
        { ids::replaceLookahead, "Replace Lookahead", &D::replaceLookaheadMs, 0.5f, 10.0f, 0.1f, 0.0f, "ms", 1, true, false },
    };
    return specs;
}

const std::vector<DrumBoolParamSpec>& drumBoolParamSpecs()
{
    static const std::vector<DrumBoolParamSpec> specs {
        { ids::drumOn, "Drums On", &D::moduleOn, true },
        { ids::drumDebleed, "De-bleed On", &D::debleedEnabled, true },
        { ids::drumReplace, "Replace On", &D::replaceEnabled, true },
    };
    return specs;
}

namespace {

template <typename Settings>
void addFloats(juce::AudioProcessorValueTreeState::ParameterLayout& layout, const std::vector<FloatSpec<Settings>>& specs)
{
    const Settings defaults;
    for (const auto& p : specs)
    {
        juce::NormalisableRange<float> range(p.min, p.max, p.step);
        // Skew only when the centre lies strictly inside the range; 0 means
        // "linear" even for ranges that do not contain 0 (e.g. -40..-6 dBFS).
        if (p.skewCentre > p.min && p.skewCentre < p.max && std::abs(p.skewCentre) > 0.0f)
            range.setSkewForCentre(p.skewCentre);

        const int decimals = p.decimals;
        const juce::String unit(p.unit);
        const bool isRatio = juce::String(p.id) == ids::gateRatio;
        const bool offAtMin = juce::String(p.id) == ids::replaceOriginal; // -60 dB = original off
        const bool fullAtMax = juce::String(p.id) == ids::replaceLength;  // 2000 ms = whole sample
        const float lo = p.min, hi = p.max;
        auto attributes = juce::AudioParameterFloatAttributes()
                              .withLabel(unit)
                              .withAutomatable(p.automatable)
                              .withStringFromValueFunction([decimals, unit, isRatio, offAtMin, fullAtMax, lo, hi](float v, int) {
                                  if (isRatio && v >= gate::kHardGateRatio - 0.05f)
                                      return juce::String("Gate");
                                  if (offAtMin && v <= lo + 0.05f)
                                      return juce::String("Off");
                                  if (fullAtMax && v >= hi - 0.5f)
                                      return juce::String("Full");
                                  return juce::String(v, decimals) + (unit.isEmpty() ? "" : (unit == ":1" ? unit : " " + unit));
                              })
                              .withValueFromStringFunction([isRatio](const juce::String& t) {
                                  if (isRatio && t.trim().equalsIgnoreCase("gate"))
                                      return gate::kHardGateRatio;
                                  return t.getFloatValue();
                              });

        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { p.id, 1 }, p.name, range,
                                                               defaults.*p.member, attributes));
    }
}

template <typename Settings>
void addBools(juce::AudioProcessorValueTreeState::ParameterLayout& layout, const std::vector<BoolSpec<Settings>>& specs)
{
    const Settings defaults;
    for (const auto& b : specs)
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { b.id, 1 }, b.name, defaults.*b.member,
            juce::AudioParameterBoolAttributes().withAutomatable(b.automatable)));
}

template <typename Settings, typename Spec, typename Vec>
void bind(juce::AudioProcessorValueTreeState& state, const std::vector<Spec>& specs, Vec& out)
{
    for (const auto& p : specs)
    {
        auto* raw = state.getRawParameterValue(p.id);
        jassert(raw != nullptr);
        out.emplace_back(raw, p.member);
    }
}

} // namespace

juce::String eqParamId(int band, EqField field)
{
    static const char* suffix[] = { "On", "Freq", "Gain", "Q", "Slope" };
    return "eqB" + juce::String(band) + suffix[static_cast<int>(field)];
}

bool eqBandHasGain(int band)
{
    const auto t = eq::EqSettings::type(band);
    return t != eq::BandType::HighPass && t != eq::BandType::LowPass;
}

bool eqBandHasSlope(int band) { return !eqBandHasGain(band); }

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    addFloats(layout, floatParamSpecs());
    addBools(layout, boolParamSpecs());
    addFloats(layout, gateFloatParamSpecs());
    addBools(layout, gateBoolParamSpecs());
    addFloats(layout, drumFloatParamSpecs());
    addBools(layout, drumBoolParamSpecs());

    // Module power for Smart Gain (gate and de-bleed have their own on/off).
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ids::smartGainOn, 1 }, "Smart Gain On", true));
    // Source type: wording only (Mix Coach, status). A setting, not automation.
    juce::StringArray sources;
    for (int i = 0; i < gain::kNumSourceTypes; ++i)
        sources.add(gain::words(gain::sourceFromIndex(i)).name);
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { ids::sourceType, 1 }, "Source", sources, 0,
                                                            juce::AudioParameterChoiceAttributes().withAutomatable(false)));

    // ---- EQ -----------------------------------------------------------------
    const eq::EqSettings eqDefaults;
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ids::eqOn, 1 }, "EQ On", eqDefaults.enabled));
    const auto dbText = [](float v, int) { return (v > 0.0f ? "+" : "") + juce::String(v, 1) + " dB"; };
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ids::eqOutput, 1 }, "EQ Output", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), eqDefaults.outputDb,
        juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(dbText)));
    for (int b = 0; b < eq::kNumBands; ++b)
    {
        const auto& d = eqDefaults.bands[static_cast<size_t>(b)];
        const juce::String name = juce::String("EQ ") + eq::EqSettings::bandName(b);
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { eqParamId(b, EqField::On), 1 }, name + " On", d.on));
        juce::NormalisableRange<float> fr(20.0f, 20000.0f, 1.0f);
        fr.setSkewForCentre(632.0f); // geometric centre of 20 Hz .. 20 kHz
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { eqParamId(b, EqField::Freq), 1 }, name + " Freq", fr, d.freqHz,
            juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction([](float v, int) {
                return v >= 1000.0f ? juce::String(v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
            })));
        if (eqBandHasGain(b))
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { eqParamId(b, EqField::Gain), 1 }, name + " Gain", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f),
                d.gainDb, juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(dbText)));
        juce::NormalisableRange<float> qr(0.1f, 10.0f, 0.01f);
        qr.setSkewForCentre(1.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { eqParamId(b, EqField::Q), 1 }, name + " Q", qr, d.q,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return juce::String(v, 2); })));
        if (eqBandHasSlope(b))
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID { eqParamId(b, EqField::Slope), 1 }, name + " Slope",
                juce::StringArray { "12 dB/oct", "24 dB/oct", "48 dB/oct" }, d.slope < 18.0f ? 0 : (d.slope < 36.0f ? 1 : 2)));
    }

    // ---- Compressor ------------------------------------------------------------
    const comp::CompSettings cd;
    const auto F = juce::AudioParameterFloatAttributes();
    const auto msText = [](float v, int) { return v < 10.0f ? juce::String(v, 1) + " ms" : juce::String(juce::roundToInt(v)) + " ms"; };
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ids::compOn, 1 }, "Comp On", cd.enabled));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ids::compThreshold, 1 }, "Comp Threshold", juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), cd.thresholdDb,
        F.withLabel("dB").withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " dB"; })));
    {
        juce::NormalisableRange<float> r(1.0f, 20.0f, 0.1f);
        r.setSkewForCentre(4.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { ids::compRatio, 1 }, "Comp Ratio", r, cd.ratio,
            F.withStringFromValueFunction([](float v, int) { return v >= 19.95f ? juce::String("20:1 (limit)") : juce::String(v, 1) + ":1"; })));
    }
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ids::compKnee, 1 }, "Comp Knee", juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), cd.kneeDb,
        F.withLabel("dB").withStringFromValueFunction([](float v, int) { return v < 0.05f ? juce::String("Hard") : juce::String(v, 1) + " dB"; })));
    {
        juce::NormalisableRange<float> r(0.1f, 100.0f, 0.1f);
        r.setSkewForCentre(10.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ids::compAttack, 1 }, "Comp Attack", r, cd.attackMs,
                                                               F.withLabel("ms").withStringFromValueFunction(msText)));
    }
    {
        juce::NormalisableRange<float> r(10.0f, 2000.0f, 1.0f);
        r.setSkewForCentre(200.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ids::compRelease, 1 }, "Comp Release", r, cd.releaseMs,
                                                               F.withLabel("ms").withStringFromValueFunction(msText)));
    }
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ids::compAutoRelease, 1 }, "Comp Auto Release", cd.autoRelease));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ids::compMakeup, 1 }, "Comp Makeup", juce::NormalisableRange<float>(-12.0f, 24.0f, 0.1f), cd.makeupDb,
        F.withLabel("dB").withStringFromValueFunction(dbText)));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ids::compAutoMakeup, 1 }, "Comp Auto Makeup", cd.autoMakeup));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ids::compMix, 1 }, "Comp Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), cd.mixPercent,
        F.withLabel("%").withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " %"; })));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ids::compScHpfOn, 1 }, "Comp Sidechain HPF", cd.scHpfOn));
    {
        juce::NormalisableRange<float> r(20.0f, 500.0f, 1.0f);
        r.setSkewForCentre(100.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { ids::compScHpf, 1 }, "Comp Sidechain HPF Freq", r, cd.scHpfHz,
            F.withLabel("Hz").withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)) + " Hz"; })));
    }
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { ids::compDetector, 1 }, "Comp Detector",
                                                            juce::StringArray { "Peak", "RMS" }, static_cast<int>(cd.detector)));
    return layout;
}

ParameterReader::ParameterReader(juce::AudioProcessorValueTreeState& state)
{
    bind<S>(state, floatParamSpecs(), floats_);
    bind<S>(state, boolParamSpecs(), bools_);
    bind<G>(state, gateFloatParamSpecs(), gateFloats_);
    bind<G>(state, gateBoolParamSpecs(), gateBools_);
    bind<D>(state, drumFloatParamSpecs(), drumFloats_);
    bind<D>(state, drumBoolParamSpecs(), drumBools_);
    eqOn_ = state.getRawParameterValue(ids::eqOn);
    eqOut_ = state.getRawParameterValue(ids::eqOutput);
    comp_.on = state.getRawParameterValue(ids::compOn);
    comp_.threshold = state.getRawParameterValue(ids::compThreshold);
    comp_.ratio = state.getRawParameterValue(ids::compRatio);
    comp_.knee = state.getRawParameterValue(ids::compKnee);
    comp_.attack = state.getRawParameterValue(ids::compAttack);
    comp_.release = state.getRawParameterValue(ids::compRelease);
    comp_.autoRelease = state.getRawParameterValue(ids::compAutoRelease);
    comp_.makeup = state.getRawParameterValue(ids::compMakeup);
    comp_.autoMakeup = state.getRawParameterValue(ids::compAutoMakeup);
    comp_.mix = state.getRawParameterValue(ids::compMix);
    comp_.hpfOn = state.getRawParameterValue(ids::compScHpfOn);
    comp_.hpf = state.getRawParameterValue(ids::compScHpf);
    comp_.detector = state.getRawParameterValue(ids::compDetector);
    jassert(comp_.detector != nullptr && comp_.on != nullptr);
    for (int b = 0; b < eq::kNumBands; ++b)
    {
        auto& r = eqBands_[static_cast<size_t>(b)];
        r.on = state.getRawParameterValue(eqParamId(b, EqField::On));
        r.freq = state.getRawParameterValue(eqParamId(b, EqField::Freq));
        r.gain = eqBandHasGain(b) ? state.getRawParameterValue(eqParamId(b, EqField::Gain)) : nullptr;
        r.q = state.getRawParameterValue(eqParamId(b, EqField::Q));
        r.slope = eqBandHasSlope(b) ? state.getRawParameterValue(eqParamId(b, EqField::Slope)) : nullptr;
        jassert(r.on != nullptr && r.freq != nullptr && r.q != nullptr);
    }
    gainOn_ = state.getRawParameterValue(ids::smartGainOn);
    source_ = state.getRawParameterValue(ids::sourceType);
    jassert(gainOn_ != nullptr && source_ != nullptr);
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

gate::GateSettings ParameterReader::readGate() const noexcept
{
    G g;
    for (const auto& [raw, member] : gateFloats_)
        g.*member = raw->load(std::memory_order_relaxed);
    for (const auto& [raw, member] : gateBools_)
        g.*member = raw->load(std::memory_order_relaxed) >= 0.5f;
    return g;
}

} // namespace mixcore::plugin

namespace mixcore::plugin {

drum::DrumSettings ParameterReader::readDrum() const noexcept
{
    D d;
    for (const auto& [raw, member] : drumFloats_)
        d.*member = raw->load(std::memory_order_relaxed);
    for (const auto& [raw, member] : drumBools_)
        d.*member = raw->load(std::memory_order_relaxed) >= 0.5f;
    return d;
}

} // namespace mixcore::plugin

namespace mixcore::plugin {

eq::EqSettings ParameterReader::readEq() const noexcept
{
    eq::EqSettings e;
    e.enabled = eqOn_->load(std::memory_order_relaxed) >= 0.5f;
    e.outputDb = eqOut_->load(std::memory_order_relaxed);
    for (int b = 0; b < eq::kNumBands; ++b)
    {
        const auto& r = eqBands_[static_cast<size_t>(b)];
        auto& band = e.bands[static_cast<size_t>(b)];
        band.on = r.on->load(std::memory_order_relaxed) >= 0.5f;
        band.freqHz = r.freq->load(std::memory_order_relaxed);
        band.gainDb = r.gain != nullptr ? r.gain->load(std::memory_order_relaxed) : 0.0f;
        band.q = r.q->load(std::memory_order_relaxed);
        if (r.slope != nullptr)
        {
            const int idx = static_cast<int>(r.slope->load(std::memory_order_relaxed) + 0.5f);
            band.slope = idx <= 0 ? 12.0f : (idx == 1 ? 24.0f : 48.0f);
        }
    }
    return e;
}

comp::CompSettings ParameterReader::readComp() const noexcept
{
    const auto ld = [](const std::atomic<float>* a) { return a->load(std::memory_order_relaxed); };
    comp::CompSettings c;
    c.enabled = ld(comp_.on) >= 0.5f;
    c.thresholdDb = ld(comp_.threshold);
    c.ratio = ld(comp_.ratio);
    c.kneeDb = ld(comp_.knee);
    c.attackMs = ld(comp_.attack);
    c.releaseMs = ld(comp_.release);
    c.autoRelease = ld(comp_.autoRelease) >= 0.5f;
    c.makeupDb = ld(comp_.makeup);
    c.autoMakeup = ld(comp_.autoMakeup) >= 0.5f;
    c.mixPercent = ld(comp_.mix);
    c.scHpfOn = ld(comp_.hpfOn) >= 0.5f;
    c.scHpfHz = ld(comp_.hpf);
    c.detector = ld(comp_.detector) >= 0.5f ? comp::Detector::Rms : comp::Detector::Peak;
    return c;
}

} // namespace mixcore::plugin
