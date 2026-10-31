#pragma once
// EQ page: response curve over a live spectrum analyzer with draggable band
// nodes, band selector and controls, profiles; and the "Listen & suggest"
// panel for the right-hand column.

#include "../PluginProcessor.h"
#include "Theme.h"

#include "mixcore/eq/Spectrum.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace mixcore::plugin {

/// Curve + analyzer. Drag a node: frequency (and gain for bells/shelves).
/// Mouse wheel on a node: Q. Double-click a node: band on/off.
class EqCurve final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EqCurve(MixCoreProcessor& p);

    void setSpectrum(const std::vector<float>& inDb, const std::vector<float>& outDb);
    void setSelected(int band);
    std::function<void(int)> onSelect;

    static const std::vector<double>& displayFreqs();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

private:
    juce::Rectangle<float> plot() const;
    float xForFreq(double f) const;
    double freqForX(float x) const;
    float yForDb(double db) const;
    double dbForY(float y) const;
    juce::Point<float> nodePos(const eq::EqSettings& s, int band) const;
    int nodeAt(juce::Point<float> p) const;
    void setParam(const juce::String& id, float value, bool gesture);

    MixCoreProcessor& processor_;
    std::vector<float> inDb_, outDb_;
    int selected_ = 2;
    int dragging_ = -1;
    static constexpr double kRangeDb = 18.0;
};

class EqPanel final : public juce::Component
{
public:
    explicit EqPanel(MixCoreProcessor& processor);

    /// Called by the editor's timer: analyzer and labels.
    void update();
    void resized() override;
    void paint(juce::Graphics& g) override;
    void selectBand(int band);

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob(Knob& k, const juce::String& caption, juce::Colour accent);
    void bindBand();

    MixCoreProcessor& processor_;
    EqCurve curve_;
    juce::ComboBox profileBox_;
    juce::TextButton onButton_ { "EQ" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment_;
    std::array<juce::TextButton, eq::kNumBands> bandButtons_;
    juce::ToggleButton bandOn_ { "Band on" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bandOnAttachment_;
    Knob freq_, gain_, q_, output_;
    juce::ComboBox slopeBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> slopeAttachment_;
    juce::Label bandTitle_;
    int selected_ = 2;

    // Analyzer
    eq::Fft fft_ { 12 };
    std::vector<float> frame_;
    std::vector<float> avgIn_, avgOut_;
    std::uint64_t lastWritten_ = 0;
};

/// Right-hand column on the EQ page: Listen & suggest.
class EqSuggestPanel final : public juce::Component
{
public:
    explicit EqSuggestPanel(MixCoreProcessor& processor);
    void update();
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void rebuild();

    MixCoreProcessor& processor_;
    juce::TextButton listenButton_ { "Listen & suggest (8 s)" };
    juce::TextButton applyAllButton_ { "Apply all" };
    double progress_ = 0.0;
    juce::ProgressBar progressBar_ { progress_ };
    juce::Label intro_;
    struct Row
    {
        std::unique_ptr<juce::Label> title, why;
        std::unique_ptr<juce::TextButton> apply;
    };
    std::vector<Row> rows_;
    std::vector<eq::EqSuggestion> suggestions_;
    bool wasListening_ = false;
};

} // namespace mixcore::plugin
