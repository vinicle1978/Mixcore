#pragma once
// Gate page: profile menu, on/off, graph, main knobs and (in Advanced mode)
// key filter / ratio / hysteresis / transient / lookahead controls.
// GateLearnPanel: the "Learn threshold" box shown in the right-hand column.

#include "../PluginProcessor.h"
#include "GateHistory.h"
#include "Theme.h"

#include <memory>
#include <vector>

namespace mixcore::plugin {

class GatePanel final : public juce::Component
{
public:
    explicit GatePanel(MixCoreProcessor& processor);

    void setAdvanced(bool advanced);
    void update(const gate::GateTelemetry& t, bool fresh);
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    Knob& addKnob(const char* id, const juce::String& caption, juce::Colour accent, const juce::String& tooltip);
    void rebuildProfileMenu();

    MixCoreProcessor& processor_;
    GateHistory history_;
    juce::ComboBox profileBox_;
    juce::TextButton onButton_ { "Gate" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment_;
    juce::ToggleButton rmsToggle_ { "RMS detector" };
    juce::ToggleButton transientToggle_ { "Transient trigger" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> rmsAttachment_, transientAttachment_;
    std::vector<std::unique_ptr<Knob>> main_;
    std::vector<std::unique_ptr<Knob>> advanced_;
    bool showAdvanced_ = false;
};

class GateLearnPanel final : public juce::Component
{
public:
    explicit GateLearnPanel(MixCoreProcessor& processor);
    void update(float progress = 0.0f);
    void resized() override;

private:
    MixCoreProcessor& processor_;
    juce::TextButton learnButton_ { "Learn threshold (8 s)" };
    juce::TextButton applyButton_ { "Apply threshold" };
    juce::TextButton cancelButton_ { "Cancel" };
    double progress_ = 0.0;
    juce::ProgressBar progressBar_ { progress_ };
    juce::TextEditor text_;
    gate::LearnState lastState_ = gate::LearnState::Idle;
    gate::LearnResult result_;
};

} // namespace mixcore::plugin
