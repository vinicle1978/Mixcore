#pragma once
// VineAudio visual language for MixCore.
//
// Palette: black, white, VineAudio gold (sampled from the logo) and red, with
// gold gradients. Gold = levels and active processing; white/champagne = the
// rider and text; red is reserved for safety intervention and problems, so it
// always means "look here".

#include <juce_gui_basics/juce_gui_basics.h>

namespace mixcore::plugin::theme {

// ---- Base -----------------------------------------------------------------
inline const juce::Colour background { 0xff0a0a0b };  // near-black
inline const juce::Colour backgroundTop { 0xff17140f }; // warm black (top of gradients)
inline const juce::Colour panel { 0xff15130f };
inline const juce::Colour panelEdge { 0xff2f2820 };   // warm charcoal, tracks and dividers
inline const juce::Colour text { 0xfff7f3ec };        // white
inline const juce::Colour muted { 0xff9d9286 };       // warm grey

// ---- VineAudio gold ---------------------------------------------------------
inline const juce::Colour goldShadow { 0xff72401d };
inline const juce::Colour gold { 0xffcf9156 };
inline const juce::Colour goldLight { 0xffe9b77c };
inline const juce::Colour goldHighlight { 0xfffdde9f };

// ---- Signal roles (names kept so every component maps onto the palette) ----
inline const juce::Colour level { 0xffd9a15e };   // meters, static gain, drum body: gold
inline const juce::Colour rider { 0xfff6e6c8 };   // rider gain, thresholds: champagne white
inline const juce::Colour safety { 0xffe5383b };  // guard, clipping, problems, reduction: red
inline const juce::Colour good { 0xfff2c46d };    // active / open: bright gold
inline const juce::Colour hold { 0xff9a6438 };    // phrase hold / confirming: deep copper

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

/// Metallic gold gradient (shadow -> gold -> highlight), like the logo ribbon.
inline juce::ColourGradient goldGradient(juce::Rectangle<float> r, bool vertical = true)
{
    juce::ColourGradient grad(goldShadow, vertical ? r.getCentreX() : r.getX(), vertical ? r.getBottom() : r.getCentreY(),
                              goldHighlight, vertical ? r.getCentreX() : r.getRight(), vertical ? r.getY() : r.getCentreY(),
                              false);
    grad.addColour(0.55, gold);
    grad.addColour(0.85, goldLight);
    return grad;
}

/// Red gradient for alerts and safety (dark red -> red -> light red).
inline juce::ColourGradient redGradient(juce::Rectangle<float> r)
{
    juce::ColourGradient grad(safety.darker(0.6f), r.getCentreX(), r.getBottom(), safety.brighter(0.35f), r.getCentreX(),
                              r.getY(), false);
    grad.addColour(0.5, safety);
    return grad;
}

/// Subtle warm-black panel fill with a hairline gold edge.
inline void fillPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius = 8.0f, float edgeAlpha = 0.22f)
{
    g.setGradientFill(juce::ColourGradient(backgroundTop, r.getCentreX(), r.getY(), panel, r.getCentreX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, radius);
    g.setColour(gold.withAlpha(edgeAlpha));
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
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
        setColour(juce::TextButton::buttonColourId, panel);
        setColour(juce::TextButton::buttonOnColourId, gold);
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, background);
        setColour(juce::ToggleButton::textColourId, text);
        setColour(juce::ToggleButton::tickColourId, goldLight);
        setColour(juce::ToggleButton::tickDisabledColourId, muted);
        setColour(juce::TextEditor::backgroundColourId, panel);
        setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::textColourId, text);
        setColour(juce::TextEditor::highlightColourId, gold.withAlpha(0.35f));
        setColour(juce::ProgressBar::foregroundColourId, gold);
        setColour(juce::ProgressBar::backgroundColourId, panelEdge);
        setColour(juce::ScrollBar::thumbColourId, gold.withAlpha(0.45f));
        setColour(juce::ComboBox::backgroundColourId, panel);
        setColour(juce::ComboBox::outlineColourId, gold.withAlpha(0.35f));
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::arrowColourId, goldLight);
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff121010));
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::headerTextColourId, goldLight);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, gold.withAlpha(0.85f));
        setColour(juce::PopupMenu::highlightedTextColourId, background);
        setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff121010));
        setColour(juce::AlertWindow::textColourId, text);
        setColour(juce::AlertWindow::outlineColourId, gold.withAlpha(0.5f));
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff121010));
        setColour(juce::TooltipWindow::textColourId, text);
        setColour(juce::TooltipWindow::outlineColourId, gold.withAlpha(0.5f));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override
    {
        auto r = button.getLocalBounds().toFloat().reduced(0.5f);
        const float radius = 5.0f;
        const bool on = button.getToggleState();
        if (on)
        {
            // "On" buttons: metallic gold, or red when the button's on-colour is a warning (e.g. Bypass).
            const auto onColour = button.findColour(juce::TextButton::buttonOnColourId);
            const bool redOn = std::abs(onColour.getHue() - safety.getHue()) < 0.05f && onColour.getSaturation() > 0.5f;
            g.setGradientFill(redOn ? redGradient(r) : goldGradient(r));
            g.fillRoundedRectangle(r, radius);
        }
        else
        {
            juce::ColourGradient grad(backgroundColour.brighter(0.12f), r.getCentreX(), r.getY(), backgroundColour.darker(0.25f),
                                      r.getCentreX(), r.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(r, radius);
            g.setColour(gold.withAlpha(!button.isEnabled() ? 0.12f : (highlighted ? 0.75f : 0.32f)));
            g.drawRoundedRectangle(r, radius, 1.0f);
        }
        if (down)
        {
            g.setColour(juce::Colours::black.withAlpha(0.2f));
            g.fillRoundedRectangle(r, radius);
        }
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
        g.setGradientFill(juce::ColourGradient(panel.brighter(0.1f), 0.0f, 0.0f, panel.darker(0.3f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(gold.withAlpha(box.isMouseOver(true) ? 0.75f : 0.4f));
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
        const float ax = static_cast<float>(width) - 18.0f, ay = static_cast<float>(height) * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath(ax - 4.0f, ay - 2.0f);
        arrow.lineTo(ax, ay + 2.5f);
        arrow.lineTo(ax + 4.0f, ay - 2.0f);
        g.setColour(goldLight);
        g.strokePath(arrow, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
        const float arcR = radius - thickness;

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour(panelEdge);
        g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Bipolar controls (range spans 0) fill from the centre.
        const double min = slider.getMinimum(), max = slider.getMaximum();
        const bool bipolar = min < 0.0 && max > 0.0;
        const float zeroPos = bipolar ? static_cast<float>(slider.valueToProportionOfLength(0.0)) : 0.0f;
        const float a0 = startAngle + zeroPos * (endAngle - startAngle);
        const float a1 = startAngle + pos * (endAngle - startAngle);
        juce::Path fill;
        fill.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin(a0, a1), juce::jmax(a0, a1), true);
        // Accent gradient: deep at the bottom, highlight at the top (metallic).
        const auto arcBox = bounds.withSizeKeepingCentre(radius * 2.0f, radius * 2.0f);
        juce::ColourGradient grad(accent.darker(0.55f), arcBox.getX(), arcBox.getBottom(), accent.brighter(0.45f),
                                  arcBox.getRight(), arcBox.getY(), false);
        grad.addColour(0.5, accent);
        g.setGradientFill(grad);
        g.strokePath(fill, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Knob body: black with a soft gold rim.
        const float inner = radius - thickness * 2.2f;
        const auto body = juce::Rectangle<float>(centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a2520), body.getCentreX(), body.getY(), juce::Colour(0xff0c0b0a),
                                               body.getCentreX(), body.getBottom(), false));
        g.fillEllipse(body);
        g.setColour(gold.withAlpha(0.35f));
        g.drawEllipse(body, 1.0f);
        const juce::Point<float> tip(centre.x + inner * 0.8f * std::sin(a1), centre.y - inner * 0.8f * std::cos(a1));
        g.setColour(text);
        g.drawLine(juce::Line<float>(centre.getPointOnCircumference(inner * 0.25f, a1), tip), 2.0f);
    }

    void drawProgressBar(juce::Graphics& g, juce::ProgressBar&, int width, int height, double progress,
                         const juce::String&) override
    {
        auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        g.setColour(panelEdge);
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        if (progress > 0.0)
        {
            auto f = r.withWidth(r.getWidth() * static_cast<float>(juce::jlimit(0.0, 1.0, progress)));
            g.setGradientFill(goldGradient(r, false));
            g.fillRoundedRectangle(f, r.getHeight() * 0.5f);
        }
    }
};

/// The VineAudio logo (gold emblem + wordmark), drawn from embedded PNG data.
juce::Image logoImage();
juce::Image emblemImage();

} // namespace mixcore::plugin::theme
