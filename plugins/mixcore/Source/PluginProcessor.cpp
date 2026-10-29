#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace mixcore::plugin {

namespace {
constexpr const char* kStateTag = "MixCoreState";
constexpr int kStateVersion = 1;
} // namespace

MixCoreProcessor::MixCoreProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, kStateTag, createParameterLayout()),
      reader_(state_)
{
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
    engine_.prepare(sampleRate, juce::jmax(1, samplesPerBlock), channels);
    engine_.setSettings(reader_.read());
    engine_.reset(); // start settled with the current parameter values
    setLatencySamples(engine_.latencySamples());
}

void MixCoreProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int inputs = getTotalNumInputChannels();
    for (int ch = inputs; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    engine_.setSettings(reader_.read());
    engine_.process(buffer.getArrayOfWritePointers(), juce::jmin(inputs, buffer.getNumChannels()),
                    buffer.getNumSamples());
}

void MixCoreProcessor::timerCallback()
{
    const int latency = engine_.latencySamples();
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
    return engine_.calibrator().computeResult(s.targetRmsDb, s.peakRestrictDb);
}

void MixCoreProcessor::applyStaticGain(float db)
{
    if (auto* p = state_.getParameter(ids::staticGain))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(-18.0f, 18.0f, db)));
        p->endChangeGesture();
    }
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
    // Parameters missing from older sessions keep their defaults; the engine
    // re-validates every value before use.
}

juce::AudioProcessorEditor* MixCoreProcessor::createEditor() { return new MixCoreEditor(*this); }

} // namespace mixcore::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mixcore::plugin::MixCoreProcessor(); }
