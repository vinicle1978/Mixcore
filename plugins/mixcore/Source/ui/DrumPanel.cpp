#include "DrumPanel.h"

#include "mixcore/drum/DrumProfiles.h"

namespace mixcore::plugin {

// ------------------------------------------------------------- DebleedHistory

void DebleedHistory::push(float bodyDb, float highDb, float reductionDb, bool enabled)
{
    const auto i = static_cast<size_t>(pos_);
    body_[i] = bodyDb;
    high_[i] = highDb;
    red_[i] = reductionDb;
    enabled_ = enabled;
    pos_ = (pos_ + 1) % kPoints;
    count_ = juce::jmin(count_ + 1, kPoints);
    repaint();
}

void DebleedHistory::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    theme::fillPanel(g, area, 6.0f, 0.15f);
    auto plot = area.reduced(10.0f, 10.0f).withTrimmedLeft(24.0f);

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
        juce::Path body, high, red;
        red.startNewSubPath(plot.getRight() - static_cast<float>(count_ - 1) * dx, plot.getY());
        for (int i = 0; i < count_; ++i)
        {
            const auto idx = static_cast<size_t>((pos_ - count_ + i + kPoints) % kPoints);
            const float x = plot.getRight() - static_cast<float>(count_ - 1 - i) * dx;
            if (i == 0)
            {
                body.startNewSubPath(x, yFor(body_[idx]));
                high.startNewSubPath(x, yFor(high_[idx]));
            }
            body.lineTo(x, yFor(body_[idx]));
            high.lineTo(x, yFor(high_[idx]));
            // Reduction drawn down from the top, 0..30 dB over the plot height.
            red.lineTo(x, plot.getY() + juce::jlimit(0.0f, 1.0f, red_[idx] / 30.0f) * plot.getHeight());
        }
        red.lineTo(plot.getRight(), plot.getY());
        red.closeSubPath();
        g.setColour(theme::safety.withAlpha(enabled_ ? 0.35f : 0.1f));
        g.fillPath(red);
        g.setColour(theme::level);
        g.strokePath(body, juce::PathStrokeType(1.6f));
        g.setColour(theme::rider);
        g.strokePath(high, juce::PathStrokeType(1.2f));
    }

    g.setFont(theme::font(9.5f));
    auto legend = plot.withHeight(14.0f).withWidth(juce::jmin(plot.getWidth(), 360.0f));
    g.setColour(theme::panel.withAlpha(0.85f));
    g.fillRect(legend.expanded(2.0f, 1.0f));
    g.setColour(theme::muted);
    g.drawText(enabled_ ? "last 10 s:" : "DE-BLEED OFF - showing levels only:", legend, juce::Justification::topLeft);
    legend.removeFromLeft(enabled_ ? 58.0f : 200.0f);
    g.setColour(theme::level);
    g.drawText("drum body", legend.removeFromLeft(62.0f), juce::Justification::topLeft);
    g.setColour(theme::rider);
    g.drawText("high band", legend.removeFromLeft(62.0f), juce::Justification::topLeft);
    g.setColour(theme::safety);
    g.drawText("reduction (0-30 dB)", legend, juce::Justification::topLeft);
}

// ------------------------------------------------------------------ DrumPanel

DrumPanel::DrumPanel(MixCoreProcessor& processor) : processor_(processor)
{
    auto& apvts = processor_.state();
    addAndMakeVisible(history_);

    profileBox_.addSectionHeading("De-bleed profiles");
    const auto profiles = drum::drumProfiles();
    for (size_t i = 0; i < profiles.size(); ++i)
        profileBox_.addItem(juce::String(std::string(profiles[i].name)), static_cast<int>(i) + 1);
    profileBox_.setText("Load profile...", juce::dontSendNotification);
    profileBox_.setTooltip("Load a de-bleed starting point for this drum. Smart Gain and Gate settings are not changed.");
    profileBox_.onChange = [this] {
        const int id = profileBox_.getSelectedId();
        const auto list = drum::drumProfiles();
        if (id > 0 && id <= static_cast<int>(list.size()))
            processor_.loadDrumProfile(juce::String(std::string(list[static_cast<size_t>(id - 1)].id)));
        profileBox_.setSelectedId(0, juce::dontSendNotification);
        profileBox_.setText("Load profile...", juce::dontSendNotification);
    };
    addAndMakeVisible(profileBox_);

    onButton_.setClickingTogglesState(true);
    onButton_.setColour(juce::TextButton::buttonOnColourId, theme::good.darker(0.3f));
    onButton_.setTooltip("Switch de-bleed on or off (10 ms crossfade). Off = the drum passes through untouched.");
    onAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ids::drumDebleed, onButton_);
    addAndMakeVisible(onButton_);

    relearnButton_.setTooltip("Forget the learned drum brightness and learn it again from the next few hits. "
                              "Use after changing the drum, its tuning or the mic position.");
    relearnButton_.onClick = [this] { processor_.resetDebleedLearning(); };
    addAndMakeVisible(relearnButton_);

    addKnob(ids::drumDebleedAmount, "Amount", theme::safety,
            "Maximum reduction of high end the drum does not explain. 50 % = up to 15 dB, 100 % = up to 30 dB.");
    addKnob(ids::drumDebleedCrossover, "Crossover", theme::level,
            "Only content above this frequency is processed; the drum body below it is never changed. "
            "Snare 4-6 kHz, toms 2.5-4 kHz, kick 2-3 kHz.");
    addKnob(ids::drumDebleedLength, "Length", theme::level,
            "How long the drum's own high end may ring after each hit (snare wires, tom sustain). "
            "Shorter = more bleed removed under the tail, but a long-ringing drum loses top.");
    addKnob(ids::drumDebleedTolerance, "Tolerance", theme::rider,
            "Extra high end allowed above what the drum normally produces before anything is reduced. "
            "Raise it if a bright hit sounds dull; lower it for more reduction.");

    learnedLabel_.setFont(theme::font(12.0f, true));
    learnedLabel_.setJustificationType(juce::Justification::centredLeft);
    learnedLabel_.setColour(juce::Label::textColourId, theme::muted);
    addAndMakeVisible(learnedLabel_);
}

void DrumPanel::addKnob(const char* id, const juce::String& caption, juce::Colour accent, const juce::String& tooltip)
{
    auto knob = std::make_unique<Knob>();
    auto& k = *knob;
    knobs_.push_back(std::move(knob));
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
    k.slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setTooltip(tooltip);
    k.label.setText(caption, juce::dontSendNotification);
    k.label.setFont(theme::font(11.5f, true));
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, theme::muted);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor_.state(), id, k.slider);
    addAndMakeVisible(k.slider);
    addAndMakeVisible(k.label);
}

void DrumPanel::update(const drum::DeBleedTelemetry& t, bool fresh)
{
    if (fresh)
        history_.push(t.bodyDb, t.highDb, t.reductionDb, t.enabled);
    onButton_.setButtonText(onButton_.getToggleState() ? "De-bleed ON" : "De-bleed OFF");

    juce::String learned;
    if (t.hitsSeen == 0)
        learned = "Drum brightness: not learned yet (waiting for hits)";
    else
        learned = "Drum brightness: high band " + theme::formatDb(t.learnedRatioDb) + " dB re. body  |  "
                  + juce::String(static_cast<int>(t.hitsSeen)) + " hits";
    learnedLabel_.setText(learned, juce::dontSendNotification);
}

juce::String DrumPanel::statusText(const drum::DeBleedTelemetry& t) const
{
    juce::String s;
    if (!t.enabled)
        s << "De-bleed is off.\n\n";
    else if (t.hitsSeen < 4)
        s << "Learning the drum... play a few normal hits. Until then MixCore processes very little, so the first "
             "hits are never dulled.\n\n";
    else
        s << "Working. Current high-band reduction: " << juce::String(t.reductionDb, 1) << " dB.\n\n";

    s << "How it works\n"
         "The signal is split at the crossover. The drum body below it is never changed. Above it, MixCore keeps the "
         "high end that rises and falls with this drum's own hits (it learns how bright the drum is) and turns down "
         "the rest: hi-hat and cymbal bleed, between hits and under the drum's ringing tail.\n\n"
         "Use it with the gate: the gate removes bleed between hits; de-bleed also reduces bleed while the gate is "
         "open. No added latency, safe for live use.\n\n"
         "Limits\n"
         "During the first few milliseconds of a hit, the drum's own attack is kept even if a cymbal lands at the "
         "same moment. Bleed below the crossover (e.g. kick in the snare mic) is the gate's job. "
         "Judge the result by ear, solo and in the mix.";
    return s;
}

void DrumPanel::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(28);
    onButton_.setBounds(top.removeFromRight(120));
    top.removeFromRight(8);
    relearnButton_.setBounds(top.removeFromRight(110));
    top.removeFromRight(8);
    profileBox_.setBounds(top.removeFromLeft(juce::jmin(260, top.getWidth())));
    area.removeFromTop(6);
    learnedLabel_.setBounds(area.removeFromTop(20));
    area.removeFromTop(4);

    auto knobs = area.removeFromBottom(140);
    history_.setBounds(area.withTrimmedBottom(8));
    const int kw = knobs.getWidth() / static_cast<int>(knobs_.size());
    for (auto& k : knobs_)
    {
        auto cell = knobs.removeFromLeft(kw);
        k->label.setBounds(cell.removeFromTop(18));
        k->slider.setBounds(cell);
    }
}

} // namespace mixcore::plugin
