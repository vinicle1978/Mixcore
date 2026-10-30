#pragma once
// Module power UI: a power button for each module tab, and a banner shown on a
// module's page while it is off, so it is obvious the module is not processing.

#include "Theme.h"

#include <functional>

namespace mixcore::plugin {

/// Round power button (toggle). On = glowing blue ring, off = dim grey.
class PowerButton final : public juce::Button
{
public:
    explicit PowerButton(const juce::String& name) : juce::Button(name)
    {
        setClickingTogglesState(true);
        setTooltip("Switch " + name + " on or off. Off = not processing; audio passes through untouched.");
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        const float d = juce::jmin(r.getWidth(), r.getHeight());
        auto c = r.withSizeKeepingCentre(d, d);
        if (on)
        {
            g.setGradientFill(theme::blueGradient(c));
            g.fillEllipse(c);
            g.setColour(theme::blueLight.withAlpha(0.35f));
            g.drawEllipse(c.expanded(1.5f), 2.0f); // glow
        }
        else
        {
            g.setColour(theme::panel);
            g.fillEllipse(c);
            g.setColour(theme::muted.withAlpha(over ? 0.9f : 0.5f));
            g.drawEllipse(c, 1.2f);
        }
        // Power symbol
        const auto ctr = c.getCentre();
        const float rad = d * 0.26f;
        juce::Path arc;
        arc.addCentredArc(ctr.x, ctr.y, rad, rad, 0.0f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
        const auto ink = on ? theme::text : theme::muted.withAlpha(over ? 1.0f : 0.75f);
        g.setColour(down ? ink.withAlpha(0.6f) : ink);
        g.strokePath(arc, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine(ctr.x, ctr.y - rad - 1.5f, ctr.x, ctr.y - rad * 0.15f, 1.6f);
    }
};

/// "MODULE OFF" banner with a Turn on button. Lets clicks through everywhere
/// except its button, so settings stay adjustable while the module is off.
class ModuleOffBanner final : public juce::Component
{
public:
    ModuleOffBanner()
    {
        turnOn_.onClick = [this] {
            if (onTurnOn)
                onTurnOn();
        };
        addAndMakeVisible(turnOn_);
        setInterceptsMouseClicks(false, true);
    }

    std::function<void()> onTurnOn;

    void setModule(const juce::String& name)
    {
        name_ = name;
        turnOn_.setButtonText("Turn " + name + " on");
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(theme::background.withAlpha(0.92f));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(theme::muted.withAlpha(0.6f));
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
        auto text = r.reduced(12.0f, 0.0f).withTrimmedRight(static_cast<float>(turnOn_.getWidth()) + 12.0f);
        g.setColour(theme::text);
        g.setFont(theme::font(13.0f, true));
        g.drawText(name_.toUpperCase() + " IS OFF", text.removeFromTop(r.getHeight() * 0.5f).withTrimmedTop(3.0f),
                   juce::Justification::bottomLeft);
        g.setColour(theme::muted);
        g.setFont(theme::font(11.5f));
        g.drawText("Not processing - audio passes through untouched.", text.withTrimmedBottom(3.0f), juce::Justification::topLeft);
    }

    void resized() override { turnOn_.setBounds(getLocalBounds().removeFromRight(150).reduced(8, 7)); }

private:
    juce::String name_;
    juce::TextButton turnOn_;
};

} // namespace mixcore::plugin
