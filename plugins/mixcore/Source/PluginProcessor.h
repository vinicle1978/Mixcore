#pragma once

#include "Parameters.h"

#include "mixcore/gain/SmartGainStage.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mixcore::plugin {

class MixCoreProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    MixCoreProcessor();
    ~MixCoreProcessor() override;

    // juce::AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Lead Vocal"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    // ---- Message-thread API used by the editor ----
    juce::AudioProcessorValueTreeState& state() noexcept { return state_; }
    gain::SmartGainSettings currentSettings() const noexcept { return reader_.read(); }

    /// Single consumer: only the (one) open editor may call this.
    bool readTelemetry(gain::SmartGainTelemetry& out) noexcept { return engine_.readTelemetry(out); }

    void startCalibration(double activeSeconds) noexcept { engine_.calibrator().requestStart(activeSeconds); }
    void cancelCalibration() noexcept { engine_.calibrator().cancel(); }
    gain::CalibrationState calibrationState() const noexcept { return engine_.calibrator().state(); }
    gain::CalibrationResult computeCalibrationResult() const;

    /// Sets static gain as a host-visible, undoable parameter change.
    void applyStaticGain(float db);

private:
    void timerCallback() override; // keeps reported latency in sync

    juce::AudioProcessorValueTreeState state_;
    ParameterReader reader_;
    gain::SmartGainStage engine_;
};

} // namespace mixcore::plugin
