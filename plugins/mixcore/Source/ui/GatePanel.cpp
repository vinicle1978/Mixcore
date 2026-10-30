#include "GatePanel.h"

#include "mixcore/gate/GateProfiles.h"

namespace mixcore::plugin {

// ------------------------------------------------------------------ GatePanel

GatePanel::GatePanel(MixCoreProcessor& processor) : processor_(processor)
{
    auto& apvts = processor_.state();
    addAndMakeVisible(history_);

    rebuildProfileMenu();
    profileBox_.setTooltip("Load a gate profile for this source. Smart Gain settings are not changed. "
                           "After loading, use Learn threshold to fit it to your room.");
    profileBox_.onChange = [this] {
        const int id = profileBox_.getSelectedId();
        const auto& list = gate::gateProfiles();
        if (id > 0 && id <= static_cast<int>(list.size()))
            processor_.loadGateProfile(juce::String(list[static_cast<size_t>(id - 1)].name));
        profileBox_.setSelectedId(0, juce::dontSendNotification);
        profileBox_.setText("Load profile...", juce::dontSendNotification);
    };
    addAndMakeVisible(profileBox_);

    onButton_.setClickingTogglesState(true);
    onButton_.setColour(juce::TextButton::buttonOnColourId, theme::good.darker(0.3f));
    onButton_.setTooltip("Switch the gate on or off (10 ms crossfade).");
    onAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ids::gateEnabled, onButton_);
    addAndMakeVisible(onButton_);

    addKnob(ids::gateThreshold, "Threshold", theme::rider,
            "The gate opens when the key level reaches this. Use Learn threshold to set it from the room.");
    addKnob(ids::gateRange, "Range", theme::safety,
            "How far the mic is turned down when closed. 10 dB = gentle, 40+ dB = nearly muted.");
    addKnob(ids::gateAttack, "Attack", theme::level, "How fast the gate opens. Drums: 0.1 ms. Vocals: 2-5 ms.");
    addKnob(ids::gateHold, "Hold", theme::level, "How long it stays open after the level drops. Protects word endings and drum sustain.");
    addKnob(ids::gateRelease, "Release", theme::level, "How long it takes to close fully after the hold.");

    for (const auto& spec : gateFloatParamSpecs())
    {
        if (!spec.advanced)
            continue;
        juce::String tip;
        const juce::String id(spec.id);
        if (id == ids::gateRatio)
            tip = "Below threshold, attenuation grows by (ratio - 1) dB per dB. 20 = hard gate; 1.5-3 = gentle expander for vocals.";
        else if (id == ids::gateHysteresis)
            tip = "Closes this many dB below the threshold, so levels hovering at the threshold do not chatter.";
        else if (id == ids::gateScHpf)
            tip = "Key filter: the gate ignores content below this frequency (e.g. 150 Hz on snare ignores kick bleed).";
        else if (id == ids::gateScLpf)
            tip = "Key filter: the gate ignores content above this frequency (e.g. 150 Hz on kick ignores snare and cymbals).";
        else if (id == ids::gateTransientDb)
            tip = "With transient trigger on, the level must jump this much to open the gate (rejects cymbal wash).";
        else if (id == ids::gateLookahead)
            tip = "Opens this much before a hit arrives so the attack is not lost. Adds the same latency (reported to the host).";
        auto knob = std::make_unique<Knob>();
        auto& k = *knob;
        advanced_.push_back(std::move(knob));
        k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 15);
        k.slider.setColour(juce::Slider::rotarySliderFillColourId, theme::level);
        k.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k.slider.setTooltip(tip);
        k.label.setText(juce::String(spec.name).fromFirstOccurrenceOf("Gate ", false, false), juce::dontSendNotification);
        k.label.setFont(theme::font(12.0f, true));
        k.label.setJustificationType(juce::Justification::centred);
        k.label.setColour(juce::Label::textColourId, theme::label);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, spec.id, k.slider);
        addChildComponent(k.slider);
        addChildComponent(k.label);
    }

    rmsToggle_.setTooltip("Smooth RMS detection for voices. Off = fast peak detection for drums.");
    transientToggle_.setTooltip("Only open on a sharp rise in level. Rejects sustained bleed such as cymbal wash.");
    rmsAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ids::gateRms, rmsToggle_);
    transientAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ids::gateTransient, transientToggle_);
    addChildComponent(rmsToggle_);
    addChildComponent(transientToggle_);
}

GatePanel::Knob& GatePanel::addKnob(const char* id, const juce::String& caption, juce::Colour accent, const juce::String& tooltip)
{
    auto knob = std::make_unique<Knob>();
    auto& k = *knob;
    main_.push_back(std::move(knob));
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
    k.slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setTooltip(tooltip);
    k.label.setText(caption, juce::dontSendNotification);
    k.label.setFont(theme::font(12.5f, true));
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, theme::label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor_.state(), id, k.slider);
    addAndMakeVisible(k.slider);
    addAndMakeVisible(k.label);
    return k;
}

void GatePanel::rebuildProfileMenu()
{
    profileBox_.clear(juce::dontSendNotification);
    profileBox_.addSectionHeading("Voices");
    const auto& list = gate::gateProfiles();
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].name == "Kick")
        {
            profileBox_.addSeparator();
            profileBox_.addSectionHeading("Drums");
        }
        if (list[i].name == "Instrument")
        {
            profileBox_.addSeparator();
            profileBox_.addSectionHeading("Other");
        }
        profileBox_.addItem(juce::String(list[i].name), static_cast<int>(i) + 1);
        profileBox_.setItemEnabled(static_cast<int>(i) + 1, true);
    }
    profileBox_.setText("Load profile...", juce::dontSendNotification);
}

void GatePanel::setAdvanced(bool advanced)
{
    showAdvanced_ = advanced;
    for (auto& k : advanced_)
    {
        k->slider.setVisible(advanced);
        k->label.setVisible(advanced);
    }
    rmsToggle_.setVisible(advanced);
    transientToggle_.setVisible(advanced);
    resized();
}

void GatePanel::update(const gate::GateTelemetry& t, bool fresh)
{
    if (fresh)
        history_.push(t.detectorDb, t.thresholdDb, t.closeThresholdDb, t.gainReductionDb, static_cast<int>(t.state), t.enabled);
    onButton_.setButtonText(onButton_.getToggleState() ? "Gate ON" : "Gate OFF");
}

void GatePanel::paint(juce::Graphics&) {}

void GatePanel::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(28);
    onButton_.setBounds(top.removeFromRight(110));
    top.removeFromRight(8);
    profileBox_.setBounds(top.removeFromLeft(juce::jmin(260, top.getWidth())));
    area.removeFromTop(8);

    if (!showAdvanced_)
    {
        auto knobs = area.removeFromBottom(140);
        history_.setBounds(area.withTrimmedBottom(8));
        const int kw = knobs.getWidth() / static_cast<int>(main_.size());
        for (auto& k : main_)
        {
            auto cell = knobs.removeFromLeft(kw);
            k->label.setBounds(cell.removeFromTop(18));
            k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
            k->slider.setBounds(cell);
        }
    }
    else
    {
        history_.setBounds(area.removeFromTop(juce::jmax(80, area.getHeight() - 92 - 6 - 190)));
        area.removeFromTop(6);
        auto row = area.removeFromTop(92);
        const int kw = row.getWidth() / static_cast<int>(main_.size());
        for (auto& k : main_)
        {
            auto cell = row.removeFromLeft(kw);
            k->label.setBounds(cell.removeFromTop(16));
            k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 15);
            k->slider.setBounds(cell);
        }
        area.removeFromTop(6);
        auto toggles = area.removeFromBottom(26);
        rmsToggle_.setBounds(toggles.removeFromLeft(toggles.getWidth() / 2));
        transientToggle_.setBounds(toggles);
        const int cols = 3;
        const int rows = (static_cast<int>(advanced_.size()) + cols - 1) / cols;
        const int cw = area.getWidth() / cols, ch = area.getHeight() / juce::jmax(1, rows);
        for (size_t i = 0; i < advanced_.size(); ++i)
        {
            const int c = static_cast<int>(i) % cols, r = static_cast<int>(i) / cols;
            auto cell = juce::Rectangle<int>(area.getX() + c * cw, area.getY() + r * ch, cw, ch).reduced(2);
            advanced_[i]->label.setBounds(cell.removeFromTop(14));
            advanced_[i]->slider.setBounds(cell);
        }
    }
}

// ------------------------------------------------------------ GateLearnPanel

GateLearnPanel::GateLearnPanel(MixCoreProcessor& processor) : processor_(processor)
{
    learnButton_.setTooltip("Let the source play normally, including its gaps, for 8 seconds. MixCore measures the source "
                            "and the bleed in its mic and suggests a threshold. Nothing changes until you apply it.");
    learnButton_.onClick = [this] {
        result_ = {};
        processor_.startGateLearn(8.0);
        update();
    };
    applyButton_.onClick = [this] {
        if (result_.valid)
            processor_.applyGateThreshold(result_.suggestedThresholdDb, result_.suggestedHysteresisDb);
    };
    cancelButton_.onClick = [this] {
        processor_.cancelGateLearn();
        result_ = {};
        lastState_ = gate::LearnState::Idle;
        text_.setText("Learning cancelled.", false);
        update();
    };
    for (auto* b : { &learnButton_, &applyButton_, &cancelButton_ })
        addAndMakeVisible(*b);
    addAndMakeVisible(progressBar_);
    text_.setMultiLine(true, true);
    text_.setReadOnly(true);
    text_.setCaretVisible(false);
    text_.setScrollbarsShown(true);
    text_.setFont(theme::font(13.0f));
    text_.setIndents(10, 8);
    text_.setText("Learn threshold listens to this mic while the source plays (with its natural gaps) and suggests a "
                  "threshold between the source and the bleed. Calibrate Smart Gain first.",
                  false);
    addAndMakeVisible(text_);
    update();
}

void GateLearnPanel::update(float progress)
{
    progress_ = static_cast<double>(progress);
    const auto state = processor_.gateLearnState();
    if (state == gate::LearnState::Ready && lastState_ != gate::LearnState::Ready)
    {
        result_ = processor_.computeGateLearnResult();
        text_.setText(juce::String(result_.summary), false);
    }
    else if ((state == gate::LearnState::Requested || state == gate::LearnState::Listening) && lastState_ != state
             && lastState_ != gate::LearnState::Requested)
    {
        text_.setText("Listening... let the source play normally, including the gaps between hits or phrases.", false);
    }
    lastState_ = state;

    const bool running = state == gate::LearnState::Requested || state == gate::LearnState::Listening;
    learnButton_.setEnabled(!running);
    learnButton_.setButtonText(result_.valid ? "Learn again" : "Learn threshold (8 s)");
    applyButton_.setEnabled(result_.valid && !running);
    cancelButton_.setEnabled(running);
    progressBar_.setVisible(running);
}

void GateLearnPanel::resized()
{
    auto area = getLocalBounds();
    learnButton_.setBounds(area.removeFromTop(30));
    area.removeFromTop(6);
    progressBar_.setBounds(area.removeFromTop(10));
    area.removeFromTop(6);
    auto buttons = area.removeFromBottom(28);
    applyButton_.setBounds(buttons.removeFromLeft(150));
    buttons.removeFromLeft(6);
    cancelButton_.setBounds(buttons);
    area.removeFromBottom(6);
    text_.setBounds(area);
}

} // namespace mixcore::plugin
