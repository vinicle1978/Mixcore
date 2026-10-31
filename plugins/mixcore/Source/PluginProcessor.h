#pragma once

#include "Parameters.h"
#include "PresetManager.h"

#include "mixcore/eq/EqCoach.h"
#include "mixcore/strip/ChannelStrip.h"

#include <juce_audio_formats/juce_audio_formats.h>
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

    // Host program list = factory channel presets (Logic, Ableton, etc. show
    // these in their own preset menus). User presets live in MixCore's menu.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    // ---- Message-thread API used by the editor ----
    juce::AudioProcessorValueTreeState& state() noexcept { return state_; }
    gain::SmartGainSettings currentSettings() const noexcept { return reader_.read(); }
    strip::ChannelSettings currentChannelSettings() const noexcept { return reader_.readChannel(); }

    /// Single consumer each: only the (one) open editor may call these.
    bool readTelemetry(gain::SmartGainTelemetry& out) noexcept { return strip_.gain().readTelemetry(out); }
    bool readGateTelemetry(gate::GateTelemetry& out) noexcept { return strip_.gate().readTelemetry(out); }
    bool readDebleedTelemetry(drum::DeBleedTelemetry& out) noexcept { return strip_.debleed().readTelemetry(out); }
    bool readReplaceTelemetry(drum::ReplaceTelemetry& out) noexcept { return strip_.replacer().readTelemetry(out); }
    bool readEqTelemetry(eq::EqTelemetry& out) noexcept { return strip_.eq().readTelemetry(out); }

    // ---- EQ (message thread) ----
    const eq::SpectrumTap& eqInputTap() const noexcept { return strip_.eq().inputTap(); }
    const eq::SpectrumTap& eqOutputTap() const noexcept { return strip_.eq().outputTap(); }
    double eqSampleRate() const noexcept { return strip_.eq().sampleRate(); }
    /// Loads an EQ profile (EQ settings only) and switches the EQ on.
    bool loadEqProfile(const juce::String& name);
    /// Listen & suggest: analyses the signal going into the EQ.
    void startEqListen(double seconds);
    void cancelEqListen() noexcept { eqListener_.cancel(); }
    bool eqListening() const noexcept { return eqListener_.listening(); }
    bool eqListenReady() const noexcept { return eqListener_.ready(); }
    float eqListenProgress() const noexcept { return eqListener_.progress(); }
    std::vector<eq::EqSuggestion> eqSuggestions() const;
    /// Sets one band to the suggestion (undoable host parameter changes) and switches the EQ on.
    void applyEqSuggestion(const eq::EqSuggestion& s);
    /// Feeds the listener from the analyzer tap (called by the timer; public for tests).
    void pumpEqListener();

    // Smart Gain calibration
    void startCalibration(double activeSeconds) noexcept { strip_.gain().calibrator().requestStart(activeSeconds); }
    void cancelCalibration() noexcept { strip_.gain().calibrator().cancel(); }
    gain::CalibrationState calibrationState() const noexcept { return strip_.gain().calibrator().state(); }
    gain::CalibrationResult computeCalibrationResult() const;
    /// Sets static gain as a host-visible, undoable parameter change.
    void applyStaticGain(float db);

    // Gate threshold learning
    void startGateLearn(double seconds) noexcept { strip_.gate().learner().requestStart(seconds); }
    void cancelGateLearn() noexcept { strip_.gate().learner().cancel(); }
    gate::LearnState gateLearnState() const noexcept { return strip_.gate().learner().state(); }
    gate::LearnResult computeGateLearnResult() const;
    /// Sets threshold and hysteresis (undoable) and switches the gate on.
    void applyGateThreshold(float thresholdDb, float hysteresisDb);
    /// Loads a gate profile only (Smart Gain untouched) and switches the gate on.
    bool loadGateProfile(const juce::String& name);

    // Drum de-bleed
    /// Forget the learned drum brightness (after changing drum, tuning or mic).
    void resetDebleedLearning() noexcept { strip_.debleed().resetLearning(); }
    /// Loads a de-bleed profile only (by id: "snare", "toms", "kick", "off").
    bool loadDrumProfile(const juce::String& id);

    // Drum replacement kits (message thread). Each returns an error message, or empty on success.
    juce::String loadStarterKit(const juce::String& id);
    juce::String loadKitFiles(const juce::Array<juce::File>& files, const juce::String& kitName = {});
    juce::String loadKitFolder(const juce::File& folder);
    /// "starter:snare", "files:<path>\n<path>...", or empty (no kit).
    juce::String kitSource() const;
    juce::String kitName() const;
    juce::String kitDescription() const;
    /// Restores a kit from a kitSource() string (sessions, presets). Missing files are reported.
    juce::String restoreKit(const juce::String& source);

    // ---- Channel presets (message thread) ----
    PresetManager& presetManager() noexcept { return presets_; }
    void setPresetDirectory(const juce::File& dir) { presets_ = PresetManager(dir); }

    bool loadFactoryPreset(const juce::String& name);
    bool loadUserPreset(const juce::String& name);
    /// Returns an error message, or empty on success. The saved preset becomes current.
    juce::String saveUserPreset(const juce::String& name);
    bool deleteUserPreset(const juce::String& name);

    juce::String currentPresetName() const;
    bool currentPresetIsUser() const;
    /// True when settings differ from the current preset (static gain and bypass ignored).
    bool isPresetModified() const;

private:
    void setParam(const char* id, float plainValue);
    /// Pushes every preset-controlled value to the host parameters, as one
    /// undoable gesture per parameter. Static gain and bypass are untouched.
    void applySettingsToParameters(const strip::ChannelSettings& preset);
    void applyGateToParameters(const gate::GateSettings& g);
    void applyDrumToParameters(const drum::DrumSettings& d);
    void applyEqToParameters(const eq::EqSettings& e);
    void setEqBandParameters(int band, const eq::EqBand& b);
    void publishKit(std::unique_ptr<drum::SampleKit> kit, const juce::String& source);
    void setCurrentPreset(const juce::String& name, bool isUser, const strip::ChannelSettings& settings);
    void restorePresetReference();

    void timerCallback() override; // keeps reported latency in sync

    juce::AudioProcessorValueTreeState state_;
    ParameterReader reader_;
    strip::ChannelStrip strip_;
    PresetManager presets_;
    juce::AudioFormatManager formats_;
    eq::EqListener eqListener_;
    std::uint64_t eqFed_ = 0;
    std::vector<float> eqFrame_;
    strip::ChannelSettings currentPresetSettings_ {};
};

} // namespace mixcore::plugin
