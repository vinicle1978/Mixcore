#pragma once
// Scrolling gate view (last 10 s): detector level (gold), threshold and close
// threshold (champagne lines), gain reduction (red fill from the top) and an
// open/hold band along the bottom. Shows at a glance whether hits clear the
// threshold and whether bleed stays under it.

#include "Theme.h"

#include <array>

namespace mixcore::plugin {

class GateHistory final : public juce::Component
{
public:
    static constexpr int kPoints = 300; // 10 s at 30 Hz

    void push(float detectorDb, float thresholdDb, float closeDb, float reductionDb, int state, bool enabled)
    {
        const auto i = static_cast<size_t>(pos_);
        det_[i] = detectorDb;
        gr_[i] = reductionDb;
        state_[i] = static_cast<std::uint8_t>(state);
        thr_ = thresholdDb;
        close_ = closeDb;
        enabled_ = enabled;
        pos_ = (pos_ + 1) % kPoints;
        count_ = juce::jmin(count_ + 1, kPoints);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        theme::fillPanel(g, area, 6.0f, 0.15f);
        auto plot = area.reduced(10.0f, 10.0f).withTrimmedLeft(24.0f).withTrimmedBottom(6.0f);

        auto yFor = [&](float db) {
            const float n = juce::jlimit(0.0f, 1.0f, (db - kMinDb) / (0.0f - kMinDb));
            return plot.getBottom() - n * plot.getHeight();
        };
        g.setFont(theme::font(9.5f));
        for (int db : { 0, -20, -40, -60 })
        {
            const float y = yFor(static_cast<float>(db));
            g.setColour(theme::panelEdge.withAlpha(0.6f));
            g.drawHorizontalLine(static_cast<int>(y), plot.getX(), plot.getRight());
            g.setColour(theme::muted);
            g.drawText(juce::String(db), juce::Rectangle<float>(area.getX() + 4.0f, y - 6.0f, 26.0f, 12.0f),
                       juce::Justification::centredLeft);
        }

        const float dx = plot.getWidth() / static_cast<float>(kPoints - 1);
        if (count_ >= 2)
        {
            juce::Path det, gr;
            gr.startNewSubPath(plot.getRight() - static_cast<float>(count_ - 1) * dx, plot.getY());
            bool started = false;
            for (int i = 0; i < count_; ++i)
            {
                const int idx = (pos_ - count_ + i + kPoints) % kPoints;
                const float x = plot.getRight() - static_cast<float>(count_ - 1 - i) * dx;
                const auto st = state_[static_cast<size_t>(idx)];
                if (enabled_ && st != 0)
                {
                    g.setColour((st == 1 ? theme::good : theme::hold).withAlpha(0.6f));
                    g.fillRect(x, plot.getBottom() + 3.0f, dx + 0.5f, 4.0f);
                }
                const float y = yFor(det_[static_cast<size_t>(idx)]);
                if (!started)
                {
                    det.startNewSubPath(x, y);
                    started = true;
                }
                det.lineTo(x, y);
                // Gain reduction drawn down from the top, 0..60 dB over the plot height.
                const float grN = juce::jlimit(0.0f, 1.0f, gr_[static_cast<size_t>(idx)] / 60.0f);
                gr.lineTo(x, plot.getY() + grN * plot.getHeight());
            }
            gr.lineTo(plot.getRight(), plot.getY());
            gr.closeSubPath();
            g.setColour(theme::safety.withAlpha(enabled_ ? 0.35f : 0.12f));
            g.fillPath(gr);
            g.setColour(theme::level);
            g.strokePath(det, juce::PathStrokeType(1.6f));
        }

        // Thresholds
        g.setColour(theme::rider);
        g.drawHorizontalLine(static_cast<int>(yFor(thr_)), plot.getX(), plot.getRight());
        const float dash[] = { 4.0f, 4.0f };
        g.drawDashedLine(juce::Line<float>(plot.getX(), yFor(close_), plot.getRight(), yFor(close_)), dash, 2, 1.0f);
        g.setFont(theme::font(9.5f, true));
        g.drawText("threshold " + juce::String(thr_, 1), plot.withHeight(12.0f).withY(yFor(thr_) - 13.0f).withTrimmedRight(4.0f),
                   juce::Justification::centredRight);

        g.setColour(theme::muted);
        g.setFont(theme::font(9.5f));
        g.drawText(enabled_ ? "key level, threshold, gain reduction (last 10 s)" : "GATE OFF - showing key level only",
                   plot.withHeight(14.0f), juce::Justification::topLeft);
    }

private:
    static constexpr float kMinDb = -70.0f;
    std::array<float, kPoints> det_ {};
    std::array<float, kPoints> gr_ {};
    std::array<std::uint8_t, kPoints> state_ {};
    float thr_ = -40.0f;
    float close_ = -46.0f;
    bool enabled_ = false;
    int pos_ = 0;
    int count_ = 0;
};

} // namespace mixcore::plugin
