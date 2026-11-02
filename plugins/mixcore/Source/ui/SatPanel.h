#pragma once
// Saturation page: the curve with the live level, a harmonics preview, type
// buttons, controls and profiles; and the "Listen & suggest" panel for the
// right-hand column.

#include "../PluginProcessor.h"
#include "Theme.h"

#include <array>
#include <memory>
#include <vector>

namespace mixcore::plugin {

/// The saturation curve for the current type and drive (input across, output
/// up, both -1.5 .. 1.5), with the live input peak marked on it.
class SatCurve final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SatCurve(MixCoreProcessor& p);
    void setLivePeak(float peakDb, bool active);
    void paint(juce::Graphics& g) override;

private:
    MixCoreProcessor& processor_;
    float peak_ = 0.0f;
    bool active_ = false;
};

/// Harmonics a sine at the live peak level would get (2nd .. 8th), from the curve itself.
class HarmonicsView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    HarmonicsView();
    void set(const std::array<float, 7>& db, float thdDb, bool active);
    void paint(juce::Graphics& g) override;

private:
    std::array<float, 7> db_ {};
    float thd_ = -150.0f;
    bool active_ = false;
};

class SatPanel final : public juce::Component
{
public:
    explicit SatPanel(MixCoreProcessor& processor);
    void update(const sat::SatTelemetry& t, bool fresh);
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob(Knob& k, const char* paramId, const juce::String& caption, juce::Colour accent, const juce::String& tip);
    void setChoice(const char* id, int index);

    MixCoreProcessor& processor_;
    SatCurve curve_;
    HarmonicsView harmonics_;
    juce::ComboBox profileBox_;
    juce::TextButton onButton_ { "SAT" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment_;
    std::array<juce::TextButton, sat::kNumSatTypes> typeButtons_;
    juce::TextButton liveButton_ { "Live" }, studioButton_ { "Studio" };
    juce::Label qualityLabel_;
    Knob drive_, mix_, output_;
    juce::ToggleButton autoGain_ { "Auto gain" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAtt_;
    float peakHoldDb_ = -150.0f;
    int peakAge_ = 0, harmonicsCountdown_ = 0;
};

/// Right-hand column on the Sat page: Listen & suggest.
class SatSuggestPanel final : public juce::Component
{
public:
    explicit SatSuggestPanel(MixCoreProcessor& processor);
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
    std::vector<sat::SatSuggestion> suggestions_;
    bool wasListening_ = false;
};

} // namespace mixcore::plugin
