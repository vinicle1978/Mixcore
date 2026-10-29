#pragma once
// MixCore visual language: one dark theme, two signal accents (teal = level,
// amber = rider), red reserved for safety intervention and problems.

#include <juce_gui_basics/juce_gui_basics.h>

namespace mixcore::plugin::theme {

inline const juce::Colour background { 0xff121418 };
inline const juce::Colour panel { 0xff1b1e24 };
inline const juce::Colour panelEdge { 0xff2a2e36 };
inline const juce::Colour text { 0xffe6e8eb };
inline const juce::Colour muted { 0xff8a9099 };
inline const juce::Colour level { 0xff2ec4b6 };   // meters, static gain
inline const juce::Colour rider { 0xfff2a541 };   // rider gain
inline const juce::Colour safety { 0xfff25f5c };  // guard / clipping / problems
inline const juce::Colour good { 0xff5bd48a };    // active vocal
inline const juce::Colour hold { 0xff7c8cf2 };    // phrase hold / confirming

inline juce::Font font(float size, bool bold = false)
{
    return juce::Font(juce::FontOptions(size).withStyle(bold ? "Bold" : "Regular"));
}

inline juce::String formatDb(float db, int decimals = 1, bool sign = true)
{
    if (db <= -149.0f)
        return "-inf";
    return (sign && db > 0.0f ? "+" : "") + juce::String(db, decimals);
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, background);
        setColour(juce::Slider::textBoxTextColourId, text);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::rotarySliderFillColourId, level);
        setColour(juce::Slider::rotarySliderOutlineColourId, panelEdge);
        setColour(juce::Label::textColourId, text);
        setColour(juce::TextButton::buttonColourId, panelEdge);
        setColour(juce::TextButton::buttonOnColourId, level.darker(0.2f));
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, background);
        setColour(juce::ToggleButton::textColourId, text);
        setColour(juce::ToggleButton::tickColourId, level);
        setColour(juce::TextEditor::backgroundColourId, panel);
        setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::textColourId, text);
        setColour(juce::ProgressBar::foregroundColourId, level);
        setColour(juce::ProgressBar::backgroundColourId, panelEdge);
        setColour(juce::ScrollBar::thumbColourId, panelEdge.brighter(0.3f));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                          float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(w), static_cast<float>(h))
                                .reduced(4.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float thickness = juce::jmax(2.5f, radius * 0.14f);
        const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, radius - thickness, radius - thickness, 0.0f, startAngle, endAngle, true);
        g.setColour(panelEdge);
        g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Bipolar controls (range spans 0) fill from the centre.
        const double min = slider.getMinimum(), max = slider.getMaximum();
        const bool bipolar = min < 0.0 && max > 0.0;
        const float zeroPos = bipolar ? static_cast<float>(slider.valueToProportionOfLength(0.0)) : 0.0f;
        const float a0 = startAngle + zeroPos * (endAngle - startAngle);
        const float a1 = startAngle + pos * (endAngle - startAngle);
        juce::Path fill;
        fill.addCentredArc(centre.x, centre.y, radius - thickness, radius - thickness, 0.0f, juce::jmin(a0, a1),
                           juce::jmax(a0, a1), true);
        g.setColour(accent);
        g.strokePath(fill, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float inner = radius - thickness * 2.2f;
        g.setColour(panel.brighter(0.08f));
        g.fillEllipse(centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);
        const juce::Point<float> tip(centre.x + inner * 0.8f * std::sin(a1), centre.y - inner * 0.8f * std::cos(a1));
        g.setColour(text);
        g.drawLine(juce::Line<float>(centre.getPointOnCircumference(inner * 0.25f, a1), tip), 2.0f);
    }
};

} // namespace mixcore::plugin::theme
