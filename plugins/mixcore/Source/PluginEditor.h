#pragma once

#include "PluginProcessor.h"
#include "ui/Meters.h"
#include "ui/Theme.h"

#include "mixcore/gain/SmartGainCoach.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <vector>

namespace mixcore::plugin {

class MixCoreEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MixCoreEditor(MixCoreProcessor&);
    ~MixCoreEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Pull the latest telemetry and refresh every display (called by the
    /// 30 Hz timer; public so tests can drive the UI deterministically).
    void refresh();
    void setAdvanced(bool advanced);

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob(Knob& knob, const char* paramId, const juce::String& caption, juce::Colour accent,
                 const juce::String& tooltip);
    void updateCalibrationUi();
    void updateCoach(const gain::SmartGainTelemetry& t);

    MixCoreProcessor& processor_;
    theme::LookAndFeel lnf_;
    juce::TooltipWindow tooltips_ { this, 600 };

    // Header
    juce::TextButton advancedButton_ { "Advanced" };
    juce::TextButton bypassButton_ { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment_;

    // Meters & status
    LevelMeter inputMeter_ { "IN" };
    LevelMeter outputMeter_ { "OUT" };
    juce::Label inputReadout_, outputReadout_;
    GainHistory history_;
    juce::Label activityLabel_;
    juce::Label staticChip_, riderChip_, guardChip_;

    // Main controls
    Knob target_, staticGain_, strength_;
    juce::ToggleButton riderToggle_ { "Rider" };
    juce::ToggleButton guardToggle_ { "Peak guard" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> riderAttachment_, guardAttachment_;

    // Advanced controls (built from the parameter table)
    std::vector<std::unique_ptr<Knob>> advancedKnobs_;
    bool advanced_ = false;

    // Calibration
    juce::TextButton calibrateButton_ { "Calibrate (6 s of singing)" };
    juce::TextButton applyButton_ { "Apply suggestion" };
    juce::TextButton applySafeButton_ { "Apply safer gain" };
    juce::TextButton cancelCalButton_ { "Cancel" };
    double calibrationProgress_ = 0.0;
    juce::ProgressBar progressBar_ { calibrationProgress_ };
    juce::TextEditor calibrationText_;
    gain::CalibrationState lastCalState_ = gain::CalibrationState::Idle;
    gain::CalibrationResult lastResult_;

    // Mix Coach
    juce::TextEditor coachText_;
    juce::String lastCoachKey_;
    int coachCountdown_ = 0;

    gain::SmartGainTelemetry telemetry_;
};

} // namespace mixcore::plugin
