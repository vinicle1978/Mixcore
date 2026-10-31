#pragma once
// VineAudio visual language for MixCore.
//
// Palette: shades of blue with gradients, and silver. Blue = levels and active
// processing; silver/white = the rider, thresholds and text. Orange and red are
// kept for level warnings only: orange = getting hot (above the warning level),
// red = clipping, safety intervention and problems, so they always mean
// "look here".

#include <juce_gui_basics/juce_gui_basics.h>

namespace mixcore::plugin::theme {

// ---- Base -----------------------------------------------------------------
inline const juce::Colour background { 0xff070b12 };    // navy black
inline const juce::Colour backgroundTop { 0xff0f1a2b }; // deep blue (top of gradients)
inline const juce::Colour panel { 0xff0e1622 };
inline const juce::Colour panelEdge { 0xff24344b };     // steel blue, tracks and dividers
inline const juce::Colour text { 0xfff3f7fb };          // white
inline const juce::Colour muted { 0xffa3b1c2 };         // light steel grey (secondary text)
inline const juce::Colour label { 0xffd7e0ea };         // knob and section labels: bright silver

// ---- VineAudio blue ---------------------------------------------------------
inline const juce::Colour blueDeep { 0xff0b3a8c };
inline const juce::Colour blue { 0xff2f7df6 };
inline const juce::Colour blueLight { 0xff6db3ff };
inline const juce::Colour ice { 0xffc4e4ff };

// ---- Silver -----------------------------------------------------------------
inline const juce::Colour silverShadow { 0xff5d6b7c };
inline const juce::Colour silver { 0xffb4bfcc };
inline const juce::Colour silverHighlight { 0xfff1f5f9 };

// ---- Level warnings (only where levels need attention) ---------------------
inline const juce::Colour warning { 0xffff8c1a };       // orange: hot level
inline const juce::Colour safety { 0xffe5383b };        // red: clip / guard / problems / reduction

// ---- Signal roles (names kept so every component maps onto the palette) ----
inline const juce::Colour level { 0xff3d8ef5 };   // meters, static gain, drum body, key level: blue
inline const juce::Colour rider { 0xffdfe7f1 };   // rider gain, thresholds, high band: silver white
inline const juce::Colour good { 0xff4fc3f7 };    // active / open: bright sky blue
inline const juce::Colour hold { 0xff6d7fa8 };    // phrase hold / confirming: steel indigo

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

/// Metallic blue gradient (deep -> blue -> ice), bottom to top (or left to right).
inline juce::ColourGradient blueGradient(juce::Rectangle<float> r, bool vertical = true)
{
    juce::ColourGradient grad(blueDeep, vertical ? r.getCentreX() : r.getX(), vertical ? r.getBottom() : r.getCentreY(),
                              ice, vertical ? r.getCentreX() : r.getRight(), vertical ? r.getY() : r.getCentreY(), false);
    grad.addColour(0.55, blue);
    grad.addColour(0.85, blueLight);
    return grad;
}

/// Brushed silver gradient, for rims and highlights.
inline juce::ColourGradient silverGradient(juce::Rectangle<float> r)
{
    juce::ColourGradient grad(silverShadow, r.getCentreX(), r.getBottom(), silverHighlight, r.getCentreX(), r.getY(), false);
    grad.addColour(0.6, silver);
    return grad;
}

/// Orange gradient for hot levels.
inline juce::ColourGradient orangeGradient(juce::Rectangle<float> r)
{
    juce::ColourGradient grad(warning.darker(0.5f), r.getCentreX(), r.getBottom(), warning.brighter(0.3f), r.getCentreX(), r.getY(), false);
    grad.addColour(0.5, warning);
    return grad;
}

/// Red gradient for clipping and safety (dark red -> red -> light red).
inline juce::ColourGradient redGradient(juce::Rectangle<float> r)
{
    juce::ColourGradient grad(safety.darker(0.6f), r.getCentreX(), r.getBottom(), safety.brighter(0.35f), r.getCentreX(),
                              r.getY(), false);
    grad.addColour(0.5, safety);
    return grad;
}

/// Deep-blue panel fill with a hairline silver edge.
inline void fillPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius = 8.0f, float edgeAlpha = 0.22f)
{
    g.setGradientFill(juce::ColourGradient(backgroundTop, r.getCentreX(), r.getY(), panel, r.getCentreX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, radius);
    g.setColour(silver.withAlpha(edgeAlpha));
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
        setColour(juce::TextButton::buttonOnColourId, blue);
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, text);
        setColour(juce::ToggleButton::textColourId, text);
        setColour(juce::ToggleButton::tickColourId, blueLight);
        setColour(juce::ToggleButton::tickDisabledColourId, muted);
        setColour(juce::TextEditor::backgroundColourId, panel);
        setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::textColourId, text);
        setColour(juce::TextEditor::highlightColourId, blue.withAlpha(0.4f));
        setColour(juce::ProgressBar::foregroundColourId, blue);
        setColour(juce::ProgressBar::backgroundColourId, panelEdge);
        setColour(juce::ScrollBar::thumbColourId, silver.withAlpha(0.45f));
        setColour(juce::ComboBox::backgroundColourId, panel);
        setColour(juce::ComboBox::outlineColourId, silver.withAlpha(0.35f));
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::arrowColourId, silver);
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff0b1320));
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::headerTextColourId, blueLight);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, blue);
        setColour(juce::PopupMenu::highlightedTextColourId, text);
        setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff0b1320));
        setColour(juce::AlertWindow::textColourId, text);
        setColour(juce::AlertWindow::outlineColourId, silver.withAlpha(0.5f));
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff0b1320));
        setColour(juce::TooltipWindow::textColourId, text);
        setColour(juce::TooltipWindow::outlineColourId, silver.withAlpha(0.5f));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override
    {
        auto r = button.getLocalBounds().toFloat().reduced(0.5f);
        const float radius = 5.0f;
        const bool on = button.getToggleState();
        if (on)
        {
            // "On" buttons: metallic blue, or red when the button's on-colour is a warning (e.g. Bypass).
            const auto onColour = button.findColour(juce::TextButton::buttonOnColourId);
            const bool redOn = std::abs(onColour.getHue() - safety.getHue()) < 0.05f && onColour.getSaturation() > 0.5f;
            g.setGradientFill(redOn ? redGradient(r) : blueGradient(r));
            g.fillRoundedRectangle(r, radius);
        }
        else
        {
            juce::ColourGradient grad(backgroundColour.brighter(0.12f), r.getCentreX(), r.getY(), backgroundColour.darker(0.25f),
                                      r.getCentreX(), r.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(r, radius);
            g.setColour(silver.withAlpha(!button.isEnabled() ? 0.12f : (highlighted ? 0.8f : 0.35f)));
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
        g.setColour(silver.withAlpha(box.isMouseOver(true) ? 0.8f : 0.4f));
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
        const float ax = static_cast<float>(width) - 18.0f, ay = static_cast<float>(height) * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath(ax - 4.0f, ay - 2.0f);
        arrow.lineTo(ax, ay + 2.5f);
        arrow.lineTo(ax + 4.0f, ay - 2.0f);
        g.setColour(silver);
        g.strokePath(arrow, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                          float endAngle, juce::Slider& slider) override
    {
        if (slider.getProperties()["hero"])
        {
            drawHeroKnob(g, x, y, w, h, pos, startAngle, endAngle);
            return;
        }
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

        // Knob body: dark steel with a brushed silver rim.
        const float inner = radius - thickness * 2.2f;
        const auto body = juce::Rectangle<float>(centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff26303e), body.getCentreX(), body.getY(), juce::Colour(0xff090d14),
                                               body.getCentreX(), body.getBottom(), false));
        g.fillEllipse(body);
        g.setGradientFill(silverGradient(body));
        g.drawEllipse(body, 1.2f);
        const juce::Point<float> tip(centre.x + inner * 0.8f * std::sin(a1), centre.y - inner * 0.8f * std::cos(a1));
        g.setColour(text);
        g.drawLine(juce::Line<float>(centre.getPointOnCircumference(inner * 0.25f, a1), tip), 2.0f);
    }

    /// The "hero" knob (e.g. replacement Mix): bigger, thicker ring that runs
    /// from silver (original) to blue (sample), a marker at the centre (50 %),
    /// end captions, and red accents (rim, pointer, end dot, glow) to mark it as
    /// the main control.
    void drawHeroKnob(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle)
    {
        auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                             static_cast<float>(h))
                          .reduced(2.0f);
        auto captions = bounds.removeFromBottom(14.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float thickness = juce::jmax(5.0f, radius * 0.17f);
        const float arcR = radius - thickness * 0.6f;

        // Soft red glow behind the knob (the hero knob is the one red accent outside warnings).
        g.setColour(safety.withAlpha(0.10f));
        g.fillEllipse(juce::Rectangle<float>(radius * 2.2f, radius * 2.2f).withCentre(centre));

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour(panelEdge.brighter(0.15f));
        g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float a1 = startAngle + pos * (endAngle - startAngle);
        juce::Path fill;
        fill.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, a1, true);
        juce::ColourGradient grad(silver, centre.x - radius, centre.y, blueLight, centre.x + radius, centre.y, false);
        grad.addColour(0.5, ice);
        g.setGradientFill(grad);
        g.strokePath(fill, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Centre marker (50 % = both at full level).
        const float mid = 0.5f * (startAngle + endAngle);
        g.setColour(text.withAlpha(0.85f));
        g.drawLine(juce::Line<float>(centre.getPointOnCircumference(arcR + thickness * 0.7f, mid),
                                     centre.getPointOnCircumference(arcR + thickness * 1.5f, mid)),
                   2.0f);

        // Body with a brushed-silver rim.
        const float inner = arcR - thickness * 1.1f;
        const auto body = juce::Rectangle<float>(inner * 2.0f, inner * 2.0f).withCentre(centre);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2b3646), body.getCentreX(), body.getY(), juce::Colour(0xff080c13),
                                               body.getCentreX(), body.getBottom(), false));
        g.fillEllipse(body);
        g.setGradientFill(redGradient(body));
        g.drawEllipse(body, 2.2f); // red rim
        const juce::Point<float> tip(centre.x + inner * 0.82f * std::sin(a1), centre.y - inner * 0.82f * std::cos(a1));
        g.setColour(safety.brighter(0.15f));
        g.drawLine(juce::Line<float>(centre.getPointOnCircumference(inner * 0.3f, a1), tip), 3.2f); // red pointer
        // Red dot where the ring ends, so the position reads at a glance.
        g.setColour(safety);
        g.fillEllipse(juce::Rectangle<float>(thickness * 0.9f, thickness * 0.9f)
                          .withCentre(centre.getPointOnCircumference(arcR, a1)));

        // End captions.
        g.setFont(font(10.0f, true));
        g.setColour(silver);
        g.drawText("ORIGINAL", captions.withWidth(captions.getWidth() * 0.5f), juce::Justification::centredLeft);
        g.setColour(blueLight);
        g.drawText("SAMPLE", captions.withTrimmedLeft(captions.getWidth() * 0.5f), juce::Justification::centredRight);
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
            g.setGradientFill(blueGradient(r, false));
            g.fillRoundedRectangle(f, r.getHeight() * 0.5f);
        }
    }
};

/// The VineAudio logo (silver-blue emblem + wordmark), drawn from embedded PNG data.
juce::Image logoImage();
juce::Image emblemImage();

} // namespace mixcore::plugin::theme
