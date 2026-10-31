#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "mixcore/comp/CompProfiles.h"
#include "mixcore/drum/DrumProfiles.h"
#include "mixcore/drum/StarterKits.h"
#include "mixcore/eq/EqProfiles.h"
#include "mixcore/gate/GateProfiles.h"
#include "mixcore/strip/ChannelPresets.h"

namespace mixcore::plugin {

namespace {
constexpr const char* kStateTag = "MixCoreState";
constexpr int kStateVersion = 1;
constexpr const char* kPresetNameProp = "presetName";
constexpr const char* kPresetIsUserProp = "presetIsUser";
constexpr const char* kKitSourceProp = "kitSource";
constexpr const char* kKitNameProp = "kitName";
constexpr int kMaxKitFiles = 128;
constexpr double kMaxSampleSeconds = 30.0;
} // namespace

MixCoreProcessor::MixCoreProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, kStateTag, createParameterLayout()),
      reader_(state_)
{
    formats_.registerBasicFormats(); // WAV, AIFF (+ FLAC/Ogg where available)
    const auto& first = strip::channelPresets().front();
    applySettingsToParameters(first.settings); // e.g. its EQ starting curve (EQ itself off)
    setCurrentPreset(first.name, false, first.settings);
    startTimerHz(30);
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
    strip_.replacer().kits().collectGarbage(); // frees kits the audio thread has finished with
    pumpEqListener();
    pumpCompListener();
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
    setParam(ids::drumOn, 1.0f); // a profile is a request to use the drum module
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
    applyEqToParameters(target.eq);
    applyCompToParameters(target.comp);
    setParam(ids::smartGainOn, target.gainOn ? 1.0f : 0.0f);
    setParam(ids::sourceType, static_cast<float>(static_cast<int>(target.source)));
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
    if (!preset->kit.empty())
        loadStarterKit(juce::String(preset->kit)); // drum presets bring their starter kit (replacement stays off)
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
    if (preset->kit.isNotEmpty())
        restoreKit(preset->kit);
    setCurrentPreset(preset->name, true, preset->settings);
    return true;
}

juce::String MixCoreProcessor::saveUserPreset(const juce::String& name)
{
    const auto clean = PresetManager::sanitiseName(name);
    if (strip::findChannelPreset(clean.toStdString()) != nullptr)
        return "\"" + clean + "\" is a factory preset name. Please choose another name.";
    const auto settings = reader_.readChannel();
    const auto error = presets_.save(clean, settings, kitSource());
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
    // Sessions saved before the drum module had a master switch: it was on if de-bleed was.
    bool hasDrumOn = false, debleedOn = false;
    for (const auto* p : xml->getChildIterator())
    {
        if (p->getStringAttribute("id") == ids::drumOn)
            hasDrumOn = true;
        if (p->getStringAttribute("id") == ids::drumDebleed)
            debleedOn = p->getDoubleAttribute("value") >= 0.5;
    }
    state_.replaceState(juce::ValueTree::fromXml(*xml));
    if (!hasDrumOn && debleedOn)
        if (auto* p = state_.getParameter(ids::drumOn))
            p->setValueNotifyingHost(1.0f);
    if (const auto kit = state_.state.getProperty(kKitSourceProp).toString(); kit.isNotEmpty())
        restoreKit(kit);
    if (!state_.state.hasProperty(kPresetNameProp))
        state_.state.setProperty(kPresetNameProp, juce::String(strip::channelPresets().front().name), nullptr);
    restorePresetReference();
    // Parameters missing from older sessions keep their defaults; the engine
    // re-validates every value before use.
}

// --------------------------------------------------------------- EQ

void MixCoreProcessor::setEqBandParameters(int band, const eq::EqBand& b)
{
    setParam(eqParamId(band, EqField::On).toRawUTF8(), b.on ? 1.0f : 0.0f);
    setParam(eqParamId(band, EqField::Freq).toRawUTF8(), b.freqHz);
    if (eqBandHasGain(band))
        setParam(eqParamId(band, EqField::Gain).toRawUTF8(), b.gainDb);
    setParam(eqParamId(band, EqField::Q).toRawUTF8(), b.q);
    if (eqBandHasSlope(band))
        setParam(eqParamId(band, EqField::Slope).toRawUTF8(), b.slope < 18.0f ? 0.0f : (b.slope < 36.0f ? 1.0f : 2.0f));
}

void MixCoreProcessor::applyEqToParameters(const eq::EqSettings& e)
{
    setParam(ids::eqOn, e.enabled ? 1.0f : 0.0f);
    setParam(ids::eqOutput, e.outputDb);
    for (int b = 0; b < eq::kNumBands; ++b)
        setEqBandParameters(b, e.bands[static_cast<size_t>(b)]);
}

bool MixCoreProcessor::loadEqProfile(const juce::String& name)
{
    const auto* p = eq::findEqProfile(name.toStdString());
    if (p == nullptr)
        return false;
    applyEqToParameters(p->settings); // profile settings have enabled == true
    return true;
}

void MixCoreProcessor::startEqListen(double seconds)
{
    eqListener_.start(seconds, eqSampleRate());
    eqFed_ = eqInputTap().written();
}

void MixCoreProcessor::pumpEqListener()
{
    if (!eqListener_.listening())
        return;
    const int frame = eq::EqListener::frameSize();
    const auto hop = static_cast<std::uint64_t>(frame / 2);
    const auto written = eqInputTap().written();
    if (written < eqFed_ + hop || written < static_cast<std::uint64_t>(frame))
        return;
    eqFrame_.resize(static_cast<size_t>(frame));
    eqInputTap().readLatest(eqFrame_.data(), frame);
    eqListener_.feedFrame(eqFrame_.data());
    eqFed_ = written;
}

std::vector<eq::EqSuggestion> MixCoreProcessor::eqSuggestions() const
{
    const auto c = reader_.readChannel();
    return eqListener_.suggestions(c.source, c.eq);
}

void MixCoreProcessor::applyEqSuggestion(const eq::EqSuggestion& s)
{
    if (s.band < 0 || s.band >= eq::kNumBands)
        return;
    setEqBandParameters(s.band, s.settings);
    setParam(ids::eqOn, 1.0f);
}

// --------------------------------------------------------- Compressor

void MixCoreProcessor::applyCompToParameters(const comp::CompSettings& c)
{
    setParam(ids::compOn, c.enabled ? 1.0f : 0.0f);
    setParam(ids::compThreshold, c.thresholdDb);
    setParam(ids::compRatio, c.ratio);
    setParam(ids::compKnee, c.kneeDb);
    setParam(ids::compAttack, c.attackMs);
    setParam(ids::compRelease, c.releaseMs);
    setParam(ids::compAutoRelease, c.autoRelease ? 1.0f : 0.0f);
    setParam(ids::compMakeup, c.makeupDb);
    setParam(ids::compAutoMakeup, c.autoMakeup ? 1.0f : 0.0f);
    setParam(ids::compMix, c.mixPercent);
    setParam(ids::compScHpfOn, c.scHpfOn ? 1.0f : 0.0f);
    setParam(ids::compScHpf, c.scHpfHz);
    setParam(ids::compDetector, static_cast<float>(static_cast<int>(c.detector)));
}

bool MixCoreProcessor::loadCompProfile(const juce::String& name)
{
    const auto* p = comp::findCompProfile(name.toStdString());
    if (p == nullptr)
        return false;
    applyCompToParameters(p->settings); // profile settings have enabled == true
    return true;
}

void MixCoreProcessor::startCompListen(double seconds) { compListener_.start(seconds, strip_.comp().levelTap()); }

void MixCoreProcessor::pumpCompListener() { compListener_.pump(strip_.comp().levelTap()); }

std::vector<comp::CompSuggestion> MixCoreProcessor::compSuggestions() const
{
    const auto c = reader_.readChannel();
    return compListener_.suggestions(c.source, c.comp);
}

void MixCoreProcessor::applyCompSuggestion(const comp::CompSuggestion& s)
{
    // Only what the suggestion changes, so applying several suggestions (in any order) combines them.
    const auto& n = s.settings;
    const auto& b = s.base;
    if (n.thresholdDb != b.thresholdDb) setParam(ids::compThreshold, n.thresholdDb);
    if (n.ratio != b.ratio) setParam(ids::compRatio, n.ratio);
    if (n.kneeDb != b.kneeDb) setParam(ids::compKnee, n.kneeDb);
    if (n.attackMs != b.attackMs) setParam(ids::compAttack, n.attackMs);
    if (n.releaseMs != b.releaseMs) setParam(ids::compRelease, n.releaseMs);
    if (n.autoRelease != b.autoRelease) setParam(ids::compAutoRelease, n.autoRelease ? 1.0f : 0.0f);
    if (n.makeupDb != b.makeupDb) setParam(ids::compMakeup, n.makeupDb);
    if (n.autoMakeup != b.autoMakeup) setParam(ids::compAutoMakeup, n.autoMakeup ? 1.0f : 0.0f);
    if (n.mixPercent != b.mixPercent) setParam(ids::compMix, n.mixPercent);
    if (n.scHpfOn != b.scHpfOn) setParam(ids::compScHpfOn, n.scHpfOn ? 1.0f : 0.0f);
    if (n.scHpfHz != b.scHpfHz) setParam(ids::compScHpf, n.scHpfHz);
    if (n.detector != b.detector) setParam(ids::compDetector, static_cast<float>(static_cast<int>(n.detector)));
    setParam(ids::compOn, 1.0f);
}

// ------------------------------------------------------------- Kits

void MixCoreProcessor::publishKit(std::unique_ptr<drum::SampleKit> kit, const juce::String& source)
{
    const juce::String name = kit != nullptr ? juce::String(kit->name) : juce::String();
    strip_.replacer().kits().publish(std::move(kit));
    state_.state.setProperty(kKitSourceProp, source, nullptr);
    state_.state.setProperty(kKitNameProp, name, nullptr);
}

juce::String MixCoreProcessor::loadStarterKit(const juce::String& id)
{
    auto kit = drum::makeStarterKit(id.toStdString());
    if (kit == nullptr)
        return "Unknown starter kit: " + id;
    publishKit(std::move(kit), id);
    return {};
}

juce::String MixCoreProcessor::loadKitFiles(const juce::Array<juce::File>& files, const juce::String& kitName)
{
    std::vector<drum::SampleBuffer> samples;
    juce::StringArray problems, paths;
    for (const auto& f : files)
    {
        if (static_cast<int>(samples.size()) >= kMaxKitFiles)
        {
            problems.add("only the first " + juce::String(kMaxKitFiles) + " files were used");
            break;
        }
        std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(f));
        if (reader == nullptr)
        {
            problems.add(f.getFileName() + ": not a readable audio file");
            continue;
        }
        if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 1
            || static_cast<double>(reader->lengthInSamples) / reader->sampleRate > kMaxSampleSeconds)
        {
            problems.add(f.getFileName() + ": empty or longer than 30 s");
            continue;
        }
        const int n = static_cast<int>(reader->lengthInSamples);
        const int ch = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
        juce::AudioBuffer<float> buf(ch, n);
        reader->read(&buf, 0, n, 0, true, ch > 1);
        drum::SampleBuffer s;
        s.name = f.getFileNameWithoutExtension().toStdString();
        s.sampleRate = reader->sampleRate;
        s.left.assign(buf.getReadPointer(0), buf.getReadPointer(0) + n);
        if (ch > 1)
            s.right.assign(buf.getReadPointer(1), buf.getReadPointer(1) + n);
        samples.push_back(std::move(s));
        paths.add(f.getFullPathName());
    }
    if (samples.empty())
        return problems.isEmpty() ? juce::String("No audio files were selected.")
                                  : "No samples could be loaded (" + problems.joinIntoString("; ") + ").";
    const auto name = kitName.isNotEmpty() ? kitName : (files.size() == 1 ? files[0].getFileNameWithoutExtension() : juce::String("My samples"));
    auto kit = std::make_unique<drum::SampleKit>(drum::buildKit("files", name.toStdString(), std::move(samples)));
    if (kit->empty())
        return "The selected files are silent.";
    publishKit(std::move(kit), "files:" + paths.joinIntoString("\n"));
    return problems.isEmpty() ? juce::String() : "Loaded, but " + problems.joinIntoString("; ") + ".";
}

juce::String MixCoreProcessor::loadKitFolder(const juce::File& folder)
{
    auto files = folder.findChildFiles(juce::File::findFiles, true, "*.wav;*.WAV;*.aif;*.aiff;*.AIF;*.AIFF;*.flac");
    files.sort();
    if (files.isEmpty())
        return "No WAV or AIFF files in " + folder.getFileName() + ".";
    return loadKitFiles(files, folder.getFileName());
}

juce::String MixCoreProcessor::restoreKit(const juce::String& source)
{
    if (source.startsWith("starter:"))
        return loadStarterKit(source);
    if (source.startsWith("files:"))
    {
        juce::Array<juce::File> files;
        juce::StringArray missing;
        for (const auto& path : juce::StringArray::fromLines(source.fromFirstOccurrenceOf("files:", false, false)))
        {
            if (path.isEmpty())
                continue;
            const juce::File f(path);
            if (f.existsAsFile())
                files.add(f);
            else
                missing.add(f.getFileName());
        }
        const auto name = state_.state.getProperty(kKitNameProp).toString();
        if (files.isEmpty())
            return "The kit's sample files were not found (" + missing.joinIntoString(", ") + ").";
        auto error = loadKitFiles(files, name);
        if (missing.size() > 0)
            error = (error.isEmpty() ? juce::String() : error + " ") + "Missing: " + missing.joinIntoString(", ") + ".";
        return error;
    }
    return {};
}

juce::String MixCoreProcessor::kitSource() const { return state_.state.getProperty(kKitSourceProp).toString(); }

juce::String MixCoreProcessor::kitName() const { return state_.state.getProperty(kKitNameProp).toString(); }

juce::String MixCoreProcessor::kitDescription() const
{
    const auto* kit = const_cast<MixCoreProcessor*>(this)->strip_.replacer().kits().published();
    return kit == nullptr ? juce::String("No kit loaded") : juce::String(kit->name) + "  -  " + juce::String(kit->describe());
}

juce::AudioProcessorEditor* MixCoreProcessor::createEditor() { return new MixCoreEditor(*this); }

} // namespace mixcore::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mixcore::plugin::MixCoreProcessor(); }
