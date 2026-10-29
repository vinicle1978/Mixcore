#pragma once
// Meter and history components. They only display values handed to them by
// the editor (from telemetry snapshots); they never touch DSP state.

#include "Theme.h"

#include <array>

namespace mixcore::plugin {

/// Vertical level meter: RMS bar, sample-peak hold line, optional target mark.
class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LevelMeter(juce::String caption) : caption_(std::move(caption)) {}

    void setLevels(float rmsDb, float peakDb, double dtSeconds)
    {
        rmsDb_ = rmsDb;
        if (peakDb >= peakHoldDb_)
        {
            peakHoldDb_ = peakDb;
            holdSeconds_ = 1.0;
        }
        else if ((holdSeconds_ -= dtSeconds) <= 0.0)
        {
            peakHoldDb_ = juce::jmax(peakDb, peakHoldDb_ - static_cast<float>(20.0 * dtSeconds));
        }
        repaint();
    }
    void setTargetDb(float db) { targetDb_ = db; }
    void setWarningDb(float db) { warningDb_ = db; }
    void setAlert(bool on) { alert_ = on; }
    float peakHoldDb() const { return peakHoldDb_; }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour(theme::muted);
        g.setFont(theme::font(11.0f, true));
        g.drawText(caption_, area.removeFromTop(16.0f), juce::Justification::centred);

        auto bar = area.reduced(area.getWidth() * 0.28f, 2.0f);
        g.setColour(theme::panel);
        g.fillRoundedRectangle(bar, 3.0f);

        auto yFor = [&](float db) {
            const float n = juce::jlimit(0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
            return bar.getBottom() - n * bar.getHeight();
        };

        // RMS bar: teal below the warning level, amber above it.
        const float top = yFor(rmsDb_);
        g.setColour(rmsDb_ > warningDb_ ? theme::rider : theme::level);
        g.fillRoundedRectangle(bar.withTop(top), 2.0f);

        // Peak hold.
        g.setColour(peakHoldDb_ > warningDb_ ? theme::safety : theme::text);
        g.fillRect(bar.getX(), yFor(peakHoldDb_) - 1.0f, bar.getWidth(), 2.0f);

        // Target marker.
        if (targetDb_ > kMinDb)
        {
            g.setColour(theme::rider);
            const float ty = yFor(targetDb_);
            juce::Path tri;
            tri.addTriangle(bar.getX() - 7.0f, ty - 4.0f, bar.getX() - 7.0f, ty + 4.0f, bar.getX() - 1.0f, ty);
            g.fillPath(tri);
        }

        // Scale.
        g.setFont(theme::font(9.5f));
        for (int db : { 0, -6, -12, -18, -24, -36, -48 })
        {
            const float y = yFor(static_cast<float>(db));
            g.setColour(theme::background.withAlpha(0.6f));
            g.drawHorizontalLine(static_cast<int>(y), bar.getX(), bar.getRight());
            g.setColour(theme::muted);
            g.drawText(juce::String(db), juce::Rectangle<float>(bar.getRight() + 4.0f, y - 6.0f, 24.0f, 12.0f),
                       juce::Justification::centredLeft);
        }

        if (alert_)
        {
            g.setColour(theme::safety);
            g.fillRoundedRectangle(bar.withHeight(6.0f).translated(0.0f, -10.0f), 2.0f);
        }
    }

private:
    static constexpr float kMinDb = -60.0f;
    static constexpr float kMaxDb = 0.0f;
    juce::String caption_;
    float rmsDb_ = -150.0f;
    float peakHoldDb_ = -150.0f;
    double holdSeconds_ = 0.0;
    float targetDb_ = -150.0f;
    float warningDb_ = -6.0f;
    bool alert_ = false;
};

/// Scrolling history of rider gain (amber line), guard reduction (red) and
/// vocal activity (green band), so users can see what the rider did and why.
class GainHistory final : public juce::Component
{
public:
    static constexpr int kPoints = 600; // 20 s at 30 Hz

    void push(float riderDb, float guardDb, int activity)
    {
        const auto i = static_cast<size_t>(pos_);
        rider_[i] = riderDb;
        guard_[i] = guardDb;
        activity_[i] = static_cast<std::uint8_t>(activity);
        pos_ = (pos_ + 1) % kPoints;
        count_ = juce::jmin(count_ + 1, kPoints);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour(theme::panel);
        g.fillRoundedRectangle(area, 6.0f);
        auto plot = area.reduced(10.0f, 10.0f).withTrimmedLeft(24.0f);

        auto yFor = [&](float db) { return plot.getCentreY() - juce::jlimit(-12.0f, 12.0f, db) / 12.0f * plot.getHeight() * 0.5f; };
        g.setFont(theme::font(9.5f));
        for (int db : { 12, 6, 0, -6, -12 })
        {
            const float y = yFor(static_cast<float>(db));
            g.setColour(theme::panelEdge.withAlpha(db == 0 ? 1.0f : 0.5f));
            g.drawHorizontalLine(static_cast<int>(y), plot.getX(), plot.getRight());
            g.setColour(theme::muted);
            g.drawText((db > 0 ? "+" : "") + juce::String(db), juce::Rectangle<float>(area.getX() + 4.0f, y - 6.0f, 26.0f, 12.0f),
                       juce::Justification::centredLeft);
        }

        if (count_ < 2)
            return;
        const float dx = plot.getWidth() / static_cast<float>(kPoints - 1);
        juce::Path riderPath, guardPath;
        bool started = false;
        for (int i = 0; i < count_; ++i)
        {
            const int idx = (pos_ - count_ + i + kPoints) % kPoints;
            const float x = plot.getRight() - static_cast<float>(count_ - 1 - i) * dx;
            // Activity band along the bottom.
            const auto a = activity_[static_cast<size_t>(idx)];
            if (a == 2 || a == 3)
            {
                g.setColour((a == 2 ? theme::good : theme::hold).withAlpha(0.55f));
                g.fillRect(x, plot.getBottom() + 3.0f, dx + 0.5f, 4.0f);
            }
            const float ry = yFor(rider_[static_cast<size_t>(idx)]);
            const float gy = yFor(-guard_[static_cast<size_t>(idx)]);
            if (!started)
            {
                riderPath.startNewSubPath(x, ry);
                guardPath.startNewSubPath(x, yFor(0.0f));
                started = true;
            }
            riderPath.lineTo(x, ry);
            guardPath.lineTo(x, gy);
        }
        guardPath.lineTo(plot.getRight(), yFor(0.0f));
        guardPath.closeSubPath();
        g.setColour(theme::safety.withAlpha(0.45f));
        g.fillPath(guardPath);
        g.setColour(theme::rider);
        g.strokePath(riderPath, juce::PathStrokeType(2.0f));

        g.setColour(theme::muted);
        g.drawText("rider gain (last 20 s)", plot.withHeight(14.0f), juce::Justification::topRight);
    }

private:
    std::array<float, kPoints> rider_ {};
    std::array<float, kPoints> guard_ {};
    std::array<std::uint8_t, kPoints> activity_ {};
    int pos_ = 0;
    int count_ = 0;
};

} // namespace mixcore::plugin
