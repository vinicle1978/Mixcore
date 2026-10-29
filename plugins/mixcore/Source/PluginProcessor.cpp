#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "mixcore/drum/DrumProfiles.h"
#include "mixcore/gate/GateProfiles.h"
#include "mixcore/strip/ChannelPresets.h"

namespace mixcore::plugin {

namespace {
constexpr const char* kStateTag = "MixCoreState";
constexpr int kStateVersion = 1;
constexpr const char* kPresetNameProp = "presetName";
constexpr const char* kPresetIsUserProp = "presetIsUser";
} // namespace

MixCoreProcessor::MixCoreProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, kStateTag, createParameterLayout()),
      reader_(state_)
{
    const auto& first = strip::channelPresets().front();
    setCurrentPreset(first.name, false, first.settings);
    startTimerHz(20);
}

MixCoreProcessor::~MixCoreProcessor() { stopTimer(); }

bool MixCoreProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void MixCoreProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const int channels = juce::jmax(1, getTotalNumInputChannels());
    strip_.prepare(sampleRate, juce::jmax(1, samplesPerBlock), channels);
    strip_.setSettings(reader_.readChannel());
    strip_.reset(); // start settled with the current parameter values
    setLatencySamples(strip_.latencySamples());
}

void MixCoreProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int inputs = getTotalNumInputChannels();
    for (int ch = inputs; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    strip_.setSettings(reader_.readChannel());
    strip_.process(buffer.getArrayOfWritePointers(), juce::jmin(inputs, buffer.getNumChannels()),
                   buffer.getNumSamples());
}

void MixCoreProcessor::timerCallback()
{
    const int latency = strip_.latencySamples();
    if (latency != getLatencySamples())
        setLatencySamples(latency);
}

juce::AudioProcessorParameter* MixCoreProcessor::getBypassParameter() const
{
    return state_.getParameter(ids::bypass);
}

gain::CalibrationResult MixCoreProcessor::computeCalibrationResult() const
{
    const auto s = reader_.read();
    return strip_.gain().calibrator().computeResult(s.targetRmsDb, s.peakRestrictDb);
}

void MixCoreProcessor::setParam(const char* id, float plainValue)
{
    if (auto* p = state_.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(plainValue));
        p->endChangeGesture();
    }
}

void MixCoreProcessor::applyStaticGain(float db) { setParam(ids::staticGain, juce::jlimit(-18.0f, 18.0f, db)); }

gate::LearnResult MixCoreProcessor::computeGateLearnResult() const
{
    // Peak detector => percussive source (drums): threshold relative to hit peaks.
    return strip_.gate().learner().computeResult(!reader_.readGate().rmsDetector);
}

void MixCoreProcessor::applyGateThreshold(float thresholdDb, float hysteresisDb)
{
    setParam(ids::gateThreshold, juce::jlimit(-80.0f, 0.0f, thresholdDb));
    setParam(ids::gateHysteresis, juce::jlimit(0.0f, 20.0f, hysteresisDb));
    setParam(ids::gateEnabled, 1.0f);
}

void MixCoreProcessor::applyGateToParameters(const gate::GateSettings& g)
{
    for (const auto& spec : gateFloatParamSpecs())
        setParam(spec.id, g.*spec.member);
    for (const auto& spec : gateBoolParamSpecs())
        setParam(spec.id, g.*spec.member ? 1.0f : 0.0f);
}

bool MixCoreProcessor::loadGateProfile(const juce::String& name)
{
    const auto* profile = gate::findGateProfile(name.toStdString());
    if (profile == nullptr)
        return false;
    applyGateToParameters(profile->settings); // profile settings have enabled == true
    return true;
}

void MixCoreProcessor::applyDrumToParameters(const drum::DrumSettings& d)
{
    for (const auto& spec : drumFloatParamSpecs())
        setParam(spec.id, d.*spec.member);
    for (const auto& spec : drumBoolParamSpecs())
        setParam(spec.id, d.*spec.member ? 1.0f : 0.0f);
}

bool MixCoreProcessor::loadDrumProfile(const juce::String& id)
{
    const auto* profile = drum::findDrumProfile(id.toStdString());
    if (profile == nullptr)
        return false;
    applyDrumToParameters(profile->settings);
    resetDebleedLearning(); // a new drum/profile: learn its brightness afresh
    return true;
}

// ---------------------------------------------------------------- Presets

int MixCoreProcessor::getNumPrograms() { return static_cast<int>(strip::channelPresets().size()); }

int MixCoreProcessor::getCurrentProgram()
{
    if (currentPresetIsUser())
        return 0;
    const auto name = currentPresetName().toStdString();
    const auto& list = strip::channelPresets();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == name)
            return static_cast<int>(i);
    return 0;
}

void MixCoreProcessor::setCurrentProgram(int index)
{
    const auto& list = strip::channelPresets();
    if (index >= 0 && index < static_cast<int>(list.size()))
        loadFactoryPreset(list[static_cast<size_t>(index)].name);
}

const juce::String MixCoreProcessor::getProgramName(int index)
{
    const auto& list = strip::channelPresets();
    if (index >= 0 && index < static_cast<int>(list.size()))
        return list[static_cast<size_t>(index)].name;
    return {};
}

void MixCoreProcessor::applySettingsToParameters(const strip::ChannelSettings& preset)
{
    const auto target = strip::applyChannelPreset(reader_.readChannel(), preset);
    for (const auto& spec : floatParamSpecs())
        if (spec.member != &gain::SmartGainSettings::staticGainDb)
            setParam(spec.id, target.gain.*spec.member);
    for (const auto& spec : boolParamSpecs())
        if (spec.member != &gain::SmartGainSettings::bypass)
            setParam(spec.id, target.gain.*spec.member ? 1.0f : 0.0f);
    applyGateToParameters(target.gate);
    applyDrumToParameters(target.drum);
}

void MixCoreProcessor::setCurrentPreset(const juce::String& name, bool isUser, const strip::ChannelSettings& settings)
{
    state_.state.setProperty(kPresetNameProp, name, nullptr);
    state_.state.setProperty(kPresetIsUserProp, isUser, nullptr);
    currentPresetSettings_ = settings;
}

bool MixCoreProcessor::loadFactoryPreset(const juce::String& name)
{
    const auto* preset = strip::findChannelPreset(name.toStdString());
    if (preset == nullptr)
        return false;
    applySettingsToParameters(preset->settings);
    setCurrentPreset(name, false, preset->settings);
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
    return true;
}

bool MixCoreProcessor::loadUserPreset(const juce::String& name)
{
    const auto preset = presets_.load(name);
    if (!preset)
        return false;
    applySettingsToParameters(preset->settings);
    setCurrentPreset(preset->name, true, preset->settings);
    return true;
}

juce::String MixCoreProcessor::saveUserPreset(const juce::String& name)
{
    const auto clean = PresetManager::sanitiseName(name);
    if (strip::findChannelPreset(clean.toStdString()) != nullptr)
        return "\"" + clean + "\" is a factory preset name. Please choose another name.";
    const auto settings = reader_.readChannel();
    const auto error = presets_.save(clean, settings);
    if (error.isEmpty())
        setCurrentPreset(clean, true, settings);
    return error;
}

bool MixCoreProcessor::deleteUserPreset(const juce::String& name)
{
    if (!presets_.remove(name))
        return false;
    if (currentPresetIsUser() && currentPresetName() == name)
        state_.state.setProperty(kPresetIsUserProp, false, nullptr); // now just "modified" settings
    return true;
}

juce::String MixCoreProcessor::currentPresetName() const { return state_.state.getProperty(kPresetNameProp).toString(); }

bool MixCoreProcessor::currentPresetIsUser() const { return static_cast<bool>(state_.state.getProperty(kPresetIsUserProp)); }

bool MixCoreProcessor::isPresetModified() const
{
    // Host parameters snap to their step size, so allow a small tolerance.
    return !strip::matchesChannelPreset(reader_.readChannel(), currentPresetSettings_, 0.01f);
}

void MixCoreProcessor::restorePresetReference()
{
    const auto name = currentPresetName();
    if (currentPresetIsUser())
    {
        if (const auto user = presets_.load(name))
        {
            currentPresetSettings_ = user->settings;
            return;
        }
    }
    else if (const auto* factory = strip::findChannelPreset(name.toStdString()))
    {
        currentPresetSettings_ = factory->settings;
        return;
    }
    // Unknown (e.g. a deleted user preset): treat the session as its own reference.
    currentPresetSettings_ = reader_.readChannel();
}

void MixCoreProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto copy = state_.copyState();
    copy.setProperty("stateVersion", kStateVersion, nullptr);
    if (auto xml = copy.createXml())
        copyXmlToBinary(*xml, destData);
}

void MixCoreProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(state_.state.getType()))
        return; // unknown or corrupt state: keep current (safe) settings
    state_.replaceState(juce::ValueTree::fromXml(*xml));
    if (!state_.state.hasProperty(kPresetNameProp))
        state_.state.setProperty(kPresetNameProp, juce::String(strip::channelPresets().front().name), nullptr);
    restorePresetReference();
    // Parameters missing from older sessions keep their defaults; the engine
    // re-validates every value before use.
}

juce::AudioProcessorEditor* MixCoreProcessor::createEditor() { return new MixCoreEditor(*this); }

} // namespace mixcore::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mixcore::plugin::MixCoreProcessor(); }
