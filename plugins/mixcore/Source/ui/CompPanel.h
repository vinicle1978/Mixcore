#pragma once
// Compressor page: transfer curve with the live level, gain-reduction meter
// and history, controls and profiles; and the "Listen & suggest" panel for the
// right-hand column.

#include "../PluginProcessor.h"
#include "Theme.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace mixcore::plugin {

/// Static curve (input -> output, before makeup) with the live detector level.
/// Drag left/right = threshold, mouse wheel = ratio.
class CompCurve final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CompCurve(MixCoreProcessor& p);
    void setLive(float detectorDb, float grDb, bool active);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

private:
    juce::Rectangle<float> plot() const;
    float xFor(float db) const;
    float yFor(float db) const;
    float dbForX(float x) const;
    MixCoreProcessor& processor_;
    float liveDb_ = -150.0f, liveGr_ = 0.0f;
    bool active_ = false, dragging_ = false;
    static constexpr float kMinDb = -60.0f;
};

/// Gain reduction over the last ~10 s (bars down from the top) with the
/// current value as a vertical meter on the right.
class CompHistory final : public juce::Component, public juce::SettableTooltipClient
{
public:
    CompHistory();
    void push(float grDb);
    void paint(juce::Graphics& g) override;
    float peakHoldDb() const noexcept { return hold_; }

private:
    std::array<float, 300> gr_ {};
    int write_ = 0;
    float current_ = 0.0f, hold_ = 0.0f;
    int holdAge_ = 0;
};

class CompPanel final : public juce::Component
{
public:
    explicit CompPanel(MixCoreProcessor& processor);
    /// Called by the editor's timer.
    void update(const comp::CompTelemetry& t, bool fresh);
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob(Knob& k, const char* paramId, const juce::String& caption, juce::Colour accent, const juce::String& tip);

    MixCoreProcessor& processor_;
    CompCurve curve_;
    CompHistory history_;
    juce::Label grLabel_;
    juce::ComboBox profileBox_;
    juce::TextButton onButton_ { "COMP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment_;
    Knob threshold_, ratio_, attack_, release_, makeup_, mix_, knee_, hpf_;
    juce::ToggleButton autoRelease_ { "Auto release" }, autoMakeup_ { "Auto makeup" }, hpfOn_ { "Sidechain HPF" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoReleaseAtt_, autoMakeupAtt_, hpfOnAtt_;
    juce::ComboBox detectorBox_;
    juce::Label detectorLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> detectorAtt_;
};

/// Right-hand column on the Comp page: Listen & suggest.
class CompSuggestPanel final : public juce::Component
{
public:
    explicit CompSuggestPanel(MixCoreProcessor& processor);
    void update();
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void rebuild();

    MixCoreProcessor& processor_;
    juce::TextButton listenButton_ { "Listen & suggest (8 s)" };
    double progress_ = 0.0;
    juce::ProgressBar progressBar_ { progress_ };
    juce::Label intro_, stats_;
    struct Row
    {
        std::unique_ptr<juce::Label> title, why;
        std::unique_ptr<juce::TextButton> apply;
    };
    std::vector<Row> rows_;
    std::vector<comp::CompSuggestion> suggestions_;
    bool wasListening_ = false;
};

} // namespace mixcore::plugin
