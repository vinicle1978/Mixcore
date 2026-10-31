#pragma once

#include "PluginProcessor.h"
#include "ui/DrumPanel.h"
#include "ui/EqPanel.h"
#include "ui/GatePanel.h"
#include "ui/ModuleUi.h"
#include "ui/ReplacePanel.h"
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

    /// Rebuild the preset menu (factory + user presets from disk).
    void rebuildPresetMenu();

    enum class Page { SmartGain, Gate, Drums, Eq };
    void setPage(Page page);
    /// Drums page: show the Replace view (true) or the De-bleed view (false).
    void setDrumView(bool replace);
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &gateChip_)
            setPage(Page::Gate);
        else if (e.eventComponent == &debleedChip_)
            setPage(Page::Drums);
        else if (e.eventComponent == &eqChip_)
            setPage(Page::Eq);
    }

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
    void updatePresetDisplay();
    juce::Rectangle<int> statusTextArea() const;
    void onPresetChosen();
    void updateModuleStates(bool force = false);
    void setParameter(const char* id, float plainValue);
    gain::SourceType source() const;
    void promptSavePreset();
    void promptDeletePreset();

    MixCoreProcessor& processor_;
    theme::LookAndFeel lnf_;
    juce::TooltipWindow tooltips_ { this, 600 };

    // Header
    juce::ComboBox presetBox_;
    juce::TextButton savePresetButton_ { "Save" };
    juce::TextButton deletePresetButton_ { "Delete" };
    juce::StringArray userPresetNames_;   // index i  <-> combo id kUserIdBase + i
    juce::String lastPresetDisplay_;
    static constexpr int kUserIdBase = 1000;
    juce::TextButton advancedButton_ { "Advanced" };
    juce::TextButton bypassButton_ { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment_;

    // Meters & status
    LevelMeter inputMeter_ { "IN" };
    LevelMeter outputMeter_ { "OUT" };
    juce::Label inputReadout_, outputReadout_;
    GainHistory history_;
    juce::Label activityLabel_;
    juce::Label staticChip_, riderChip_, guardChip_, debleedChip_, gateChip_, eqChip_;
    juce::TextButton gainTab_ { "Smart Gain" };
    juce::TextButton gateTab_ { "Gate" };
    juce::TextButton drumTab_ { "Drums" };
    juce::TextButton eqTab_ { "EQ" };
    PowerButton gainPower_ { "Smart Gain" };
    PowerButton gatePower_ { "Gate" };
    PowerButton drumPower_ { "Drums" };
    PowerButton eqPower_ { "EQ" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gainPowerAtt_, gatePowerAtt_, drumPowerAtt_, eqPowerAtt_;
    ModuleOffBanner offBanner_;
    juce::ComboBox sourceBox_;
    juce::Label sourceLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment_;
    bool gainOn_ = true, gateOn_ = false, drumOn_ = false, eqOn_ = false;
    int lastModuleKey_ = -1;
    gain::SourceType lastSource_ = static_cast<gain::SourceType>(-1); // forces the first update
    Page page_ = Page::SmartGain;
    GatePanel gatePanel_ { processor_ };
    GateLearnPanel gateLearnPanel_ { processor_ };
    gate::GateTelemetry gateTelemetry_;
    DrumPanel drumPanel_ { processor_ };
    ReplacePanel replacePanel_ { processor_ };
    juce::TextButton debleedView_ { "De-bleed" };
    juce::TextButton replaceView_ { "Replace" };
    bool replaceViewShown_ = false;
    drum::ReplaceTelemetry replaceTelemetry_;
    int hitFlash_ = 0;
    juce::TextEditor drumInfoText_;
    drum::DeBleedTelemetry drumTelemetry_;
    juce::String lastDrumInfo_;
    EqPanel eqPanel_ { processor_ };
    EqSuggestPanel eqSuggestPanel_ { processor_ };

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
    juce::String lastCoachKey_ { "<never shown>" }; // forces the first update to draw, even "All clear"
    int coachCountdown_ = 0;

    gain::SmartGainTelemetry telemetry_;
};

} // namespace mixcore::plugin
