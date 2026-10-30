#pragma once
// Drums page: de-bleed on/off, profile menu, Amount / Crossover / Length /
// Tolerance, a 10 s history (drum body vs. high band, high-band reduction)
// and the learned drum brightness. Drum replacement (phase 2) will live here.

#include "../PluginProcessor.h"
#include "Theme.h"

#include <array>
#include <memory>
#include <vector>

namespace mixcore::plugin {

/// Scrolling de-bleed view: body (blue) and high band (silver) envelopes, and
/// the high-band reduction drawn down from the top (red).
class DebleedHistory final : public juce::Component
{
public:
    static constexpr int kPoints = 300; // 10 s at 30 Hz

    void push(float bodyDb, float highDb, float reductionDb, bool enabled);
    void paint(juce::Graphics& g) override;

private:
    static constexpr float kMinDb = -70.0f;
    std::array<float, kPoints> body_ {};
    std::array<float, kPoints> high_ {};
    std::array<float, kPoints> red_ {};
    bool enabled_ = false;
    int pos_ = 0;
    int count_ = 0;
};

class DrumPanel final : public juce::Component
{
public:
    explicit DrumPanel(MixCoreProcessor& processor);

    void update(const drum::DeBleedTelemetry& t, bool fresh);
    void resized() override;

    /// Text for the right-hand column (what de-bleed is doing right now).
    juce::String statusText(const drum::DeBleedTelemetry& t) const;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void addKnob(const char* id, const juce::String& caption, juce::Colour accent, const juce::String& tooltip);

    MixCoreProcessor& processor_;
    DebleedHistory history_;
    juce::ComboBox profileBox_;
    juce::TextButton onButton_ { "De-bleed" };
    juce::TextButton relearnButton_ { "Relearn drum" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment_;
    std::vector<std::unique_ptr<Knob>> knobs_;
    juce::Label learnedLabel_;
};

} // namespace mixcore::plugin
