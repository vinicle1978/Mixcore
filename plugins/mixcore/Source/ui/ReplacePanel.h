#pragma once
// Drums page, "Replace" view: kit choice (starter kits, your own WAV files or a
// kit folder), Live/Studio timing, detector graph with hit markers (height =
// velocity), and the trigger and sample controls.

#include "../PluginProcessor.h"
#include "Theme.h"

#include <array>
#include <memory>
#include <vector>

namespace mixcore::plugin {

/// Last 10 s: detector level (blue), threshold (silver), hits as bars (height = velocity).
class ReplaceHistory final : public juce::Component
{
public:
    static constexpr int kPoints = 300;
    void push(float detectorDb, float thresholdDb, int hits, float velocity, bool enabled);
    void paint(juce::Graphics& g) override;

private:
    static constexpr float kMinDb = -70.0f;
    std::array<float, kPoints> det_ {};
    std::array<float, kPoints> vel_ {}; // 0 = no hit in this frame
    float thr_ = -24.0f;
    bool enabled_ = false;
    int pos_ = 0, count_ = 0;
};

class ReplacePanel final : public juce::Component
{
public:
    explicit ReplacePanel(MixCoreProcessor& processor);

    void update(const drum::ReplaceTelemetry& t, bool fresh);
    void resized() override;
    /// Text for the right-hand column.
    juce::String statusText(const drum::ReplaceTelemetry& t) const;
    /// Re-reads the kit name/description (after presets or sessions load a kit).
    void refreshKitInfo();

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void addKnob(const char* id, const juce::String& caption, juce::Colour accent, const juce::String& tooltip);
    void rebuildKitMenu();
    void onKitChosen();
    void showMessage(const juce::String& text);
    void setLookahead(float ms);

    MixCoreProcessor& processor_;
    ReplaceHistory history_;
    juce::ComboBox kitBox_;
    juce::TextButton liveButton_ { "Live" };
    juce::TextButton studioButton_ { "Studio" };
    juce::TextButton onButton_ { "Replace" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment_;
    juce::Label kitInfo_;
    std::vector<std::unique_ptr<Knob>> knobs_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String lastKitSource_ { "<never>" };
    juce::String message_;
};

} // namespace mixcore::plugin
