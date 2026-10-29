#include "PluginEditor.h"

namespace mixcore::plugin {

namespace {
constexpr int kWidth = 1000;
constexpr int kHeight = 640;
constexpr int kTimerHz = 30;

juce::String activityText(gain::ActivityState a)
{
    switch (a)
    {
        case gain::ActivityState::Active: return "VOCAL ACTIVE  -  riding";
        case gain::ActivityState::Hold: return "PHRASE END  -  holding";
        case gain::ActivityState::Confirming: return "CONFIRMING";
        case gain::ActivityState::Inactive: break;
    }
    return "NO VOCAL  -  rider frozen";
}

juce::Colour activityColour(gain::ActivityState a)
{
    switch (a)
    {
        case gain::ActivityState::Active: return theme::good;
        case gain::ActivityState::Hold:
        case gain::ActivityState::Confirming: return theme::hold;
        case gain::ActivityState::Inactive: break;
    }
    return theme::muted;
}

void styleChip(juce::Label& l, juce::Colour c)
{
    l.setFont(theme::font(15.0f, true));
    l.setJustificationType(juce::Justification::centred);
    l.setColour(juce::Label::textColourId, c);
    l.setColour(juce::Label::backgroundColourId, theme::panel);
    l.setColour(juce::Label::outlineColourId, c.withAlpha(0.35f));
}

void styleReadout(juce::TextEditor& t)
{
    t.setMultiLine(true, true);
    t.setReadOnly(true);
    t.setScrollbarsShown(true);
    t.setCaretVisible(false);
    t.setFont(theme::font(13.0f));
    t.setIndents(10, 8);
}
} // namespace

MixCoreEditor::MixCoreEditor(MixCoreProcessor& p) : AudioProcessorEditor(&p), processor_(p)
{
    setLookAndFeel(&lnf_);
    auto& apvts = processor_.state();

    // Header
    advancedButton_.setClickingTogglesState(true);
    advancedButton_.setTooltip("Show timing, activity and safety parameters.");
    advancedButton_.onClick = [this] { setAdvanced(advancedButton_.getToggleState()); };
    addAndMakeVisible(advancedButton_);
    bypassButton_.setClickingTogglesState(true);
    bypassButton_.setColour(juce::TextButton::buttonOnColourId, theme::safety);
    bypassButton_.setTooltip("Bypass MixCore (10 ms crossfade, latency-compensated).");
    bypassAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ids::bypass, bypassButton_);
    addAndMakeVisible(bypassButton_);

    // Meters
    for (auto* m : { &inputMeter_, &outputMeter_ })
        addAndMakeVisible(*m);
    for (auto* l : { &inputReadout_, &outputReadout_ })
    {
        l->setFont(theme::font(11.0f));
        l->setJustificationType(juce::Justification::centred);
        l->setColour(juce::Label::textColourId, theme::muted);
        addAndMakeVisible(*l);
    }
    inputMeter_.setTooltip("Input level before MixCore. Bar = 400 ms RMS, line = sample peak.");
    outputMeter_.setTooltip("Output level. The amber marker is your target RMS.");

    // Status
    activityLabel_.setFont(theme::font(13.0f, true));
    activityLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(activityLabel_);
    styleChip(staticChip_, theme::level);
    styleChip(riderChip_, theme::rider);
    styleChip(guardChip_, theme::safety);
    staticChip_.setTooltip("Static gain: set once (manually or by calibration). Never changed automatically.");
    riderChip_.setTooltip("Rider gain: slow automatic correction while the vocal is active.");
    guardChip_.setTooltip("Peak guard reduction: safety intervention, reported separately from riding.");
    for (auto* c : { &staticChip_, &riderChip_, &guardChip_ })
        addAndMakeVisible(*c);
    addAndMakeVisible(history_);

    // Main controls
    addKnob(target_, ids::targetRms, "Target RMS", theme::rider,
            "Average level the rider aims for (dBFS RMS, full-scale sine = -3 dBFS). -18 is a starting point, not a rule.");
    addKnob(staticGain_, ids::staticGain, "Static gain", theme::level,
            "Fixed gain applied before the rider. Set it with calibration or by hand.");
    addKnob(strength_, ids::riderStrength, "Strength", theme::rider,
            "How much of the level error the rider corrects. 0.5 = moderate.");
    riderToggle_.setTooltip("Enable or disable the adaptive rider. Disabled = glides back to 0 dB.");
    guardToggle_.setTooltip("Optional 2 ms lookahead peak guard at the ceiling. Adds 2 ms latency (reported to the host).");
    riderAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ids::riderEnabled, riderToggle_);
    guardAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ids::peakGuard, guardToggle_);
    addAndMakeVisible(riderToggle_);
    addAndMakeVisible(guardToggle_);

    // Advanced controls
    for (const auto& spec : floatParamSpecs())
    {
        if (!spec.advanced)
            continue;
        auto knob = std::make_unique<Knob>();
        addKnob(*knob, spec.id, spec.name, spec.member == &gain::SmartGainSettings::peakGuardCeilingDb ? theme::safety : theme::level,
                {});
        knob->slider.setVisible(false);
        knob->label.setVisible(false);
        advancedKnobs_.push_back(std::move(knob));
    }

    // Calibration
    calibrateButton_.setTooltip("Sing or speak at performance level. MixCore measures the median active level and suggests a static gain. Nothing changes until you apply it.");
    calibrateButton_.onClick = [this] {
        lastResult_ = {};
        processor_.startCalibration(6.0);
        updateCalibrationUi();
    };
    applyButton_.onClick = [this] {
        if (lastResult_.valid)
            processor_.applyStaticGain(lastResult_.recommendedGainDb);
    };
    applySafeButton_.onClick = [this] {
        if (lastResult_.valid)
            processor_.applyStaticGain(juce::jmin(lastResult_.recommendedGainDb, lastResult_.safeGainDb));
    };
    cancelCalButton_.onClick = [this] {
        processor_.cancelCalibration();
        lastResult_ = {};
        updateCalibrationUi();
    };
    for (auto* b : { &calibrateButton_, &applyButton_, &applySafeButton_, &cancelCalButton_ })
        addAndMakeVisible(*b);
    addAndMakeVisible(progressBar_);
    styleReadout(calibrationText_);
    calibrationText_.setText("Calibration measures the typical level of your vocal and suggests a static gain that brings "
                             "it to the target. Nothing changes until you apply it.",
                             false);
    addAndMakeVisible(calibrationText_);

    styleReadout(coachText_);
    addAndMakeVisible(coachText_);

    activityLabel_.setText("Waiting for audio...", juce::dontSendNotification);
    activityLabel_.setColour(juce::Label::textColourId, theme::muted);

    setResizable(true, true);
    setResizeLimits(880, 580, 1600, 1000);
    setSize(kWidth, kHeight);
    updateCalibrationUi();
    startTimerHz(kTimerHz);
}

MixCoreEditor::~MixCoreEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void MixCoreEditor::addKnob(Knob& knob, const char* paramId, const juce::String& caption, juce::Colour accent,
                            const juce::String& tooltip)
{
    knob.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    knob.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    knob.slider.setTooltip(tooltip);
    knob.label.setText(caption, juce::dontSendNotification);
    knob.label.setFont(theme::font(11.5f, true));
    knob.label.setJustificationType(juce::Justification::centred);
    knob.label.setColour(juce::Label::textColourId, theme::muted);
    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor_.state(), paramId, knob.slider);
    addAndMakeVisible(knob.slider);
    addAndMakeVisible(knob.label);
}

void MixCoreEditor::setAdvanced(bool advanced)
{
    advanced_ = advanced;
    advancedButton_.setToggleState(advanced, juce::dontSendNotification);
    for (auto& k : advancedKnobs_)
    {
        k->slider.setVisible(advanced);
        k->label.setVisible(advanced);
    }
    resized();
}

void MixCoreEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    auto header = getLocalBounds().removeFromTop(52).reduced(18, 0);
    g.setColour(theme::text);
    g.setFont(theme::font(22.0f, true));
    g.drawText("MixCore", header.removeFromLeft(110), juce::Justification::centredLeft);
    g.setColour(theme::muted);
    g.setFont(theme::font(13.0f));
    g.drawText("Smart Gain Stage  |  Lead vocal", header.removeFromLeft(260), juce::Justification::centredLeft);

    if (telemetry_.sampleRate > 0.0)
    {
        g.setFont(theme::font(11.0f));
        g.drawText(juce::String(telemetry_.sampleRate / 1000.0, 1) + " kHz   latency " + juce::String(telemetry_.latencySamples)
                       + " smp",
                   header.removeFromLeft(220), juce::Justification::centredLeft);
    }

    // Panels
    g.setColour(theme::panel.withAlpha(0.55f));
    for (auto r : { juce::Rectangle<int>(12, 60, 150, getHeight() - 72),
                    juce::Rectangle<int>(getWidth() - 352, 60, 340, getHeight() - 72) })
        g.fillRoundedRectangle(r.toFloat(), 8.0f);

    g.setColour(theme::muted);
    g.setFont(theme::font(11.0f, true));
    g.drawText("STATIC", staticChip_.getBounds().translated(0, -16).withHeight(14), juce::Justification::centred);
    g.drawText("RIDER", riderChip_.getBounds().translated(0, -16).withHeight(14), juce::Justification::centred);
    g.drawText("PEAK GUARD", guardChip_.getBounds().translated(0, -16).withHeight(14), juce::Justification::centred);
    g.drawText("CALIBRATION", juce::Rectangle<int>(getWidth() - 340, 66, 200, 16), juce::Justification::centredLeft);
    g.drawText("MIX COACH", coachText_.getBounds().translated(0, -20).withHeight(16), juce::Justification::centredLeft);

    // Signal-flow arrows between the chips.
    g.setColour(theme::muted);
    for (auto* c : { &staticChip_, &riderChip_ })
    {
        const auto r = c->getBounds();
        g.drawArrow(juce::Line<float>(static_cast<float>(r.getRight() + 4), static_cast<float>(r.getCentreY()),
                                      static_cast<float>(r.getRight() + 18), static_cast<float>(r.getCentreY())),
                    1.5f, 6.0f, 6.0f);
    }
}

void MixCoreEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(52).reduced(18, 12);
    bypassButton_.setBounds(header.removeFromRight(90));
    header.removeFromRight(8);
    advancedButton_.setBounds(header.removeFromRight(100));

    area.reduce(12, 8);
    area.removeFromBottom(4);

    // Left: meters
    auto left = area.removeFromLeft(150).reduced(8, 8);
    auto readouts = left.removeFromBottom(40);
    inputMeter_.setBounds(left.removeFromLeft(left.getWidth() / 2));
    outputMeter_.setBounds(left);
    inputReadout_.setBounds(readouts.removeFromLeft(readouts.getWidth() / 2));
    outputReadout_.setBounds(readouts);

    // Right: calibration + coach
    auto right = area.removeFromRight(340).reduced(12, 8);
    right.removeFromTop(22);
    auto cal = right.removeFromTop(220);
    calibrateButton_.setBounds(cal.removeFromTop(30));
    cal.removeFromTop(6);
    progressBar_.setBounds(cal.removeFromTop(10));
    cal.removeFromTop(6);
    auto calButtons = cal.removeFromBottom(28);
    applyButton_.setBounds(calButtons.removeFromLeft(130));
    calButtons.removeFromLeft(6);
    applySafeButton_.setBounds(calButtons.removeFromLeft(120));
    calButtons.removeFromLeft(6);
    cancelCalButton_.setBounds(calButtons);
    cal.removeFromBottom(6);
    calibrationText_.setBounds(cal);
    right.removeFromTop(28);
    coachText_.setBounds(right);

    // Centre
    auto centre = area.reduced(16, 8);
    auto status = centre.removeFromTop(26);
    activityLabel_.setBounds(status);
    centre.removeFromTop(22);
    auto chips = centre.removeFromTop(38);
    const int chipW = (chips.getWidth() - 2 * 24) / 3;
    staticChip_.setBounds(chips.removeFromLeft(chipW));
    chips.removeFromLeft(24);
    riderChip_.setBounds(chips.removeFromLeft(chipW));
    chips.removeFromLeft(24);
    guardChip_.setBounds(chips);
    centre.removeFromTop(12);

    if (!advanced_)
    {
        auto controls = centre.removeFromBottom(150);
        history_.setBounds(centre.withTrimmedBottom(10));
        auto toggles = controls.removeFromRight(110);
        riderToggle_.setBounds(toggles.removeFromTop(controls.getHeight() / 2).withSizeKeepingCentre(110, 28));
        guardToggle_.setBounds(toggles.withSizeKeepingCentre(110, 28));
        const int kw = controls.getWidth() / 3;
        for (auto* k : { &target_, &staticGain_, &strength_ })
        {
            auto cell = controls.removeFromLeft(kw);
            k->label.setBounds(cell.removeFromTop(18));
            k->slider.setBounds(cell);
        }
    }
    else
    {
        // Advanced: compact history, main knobs in a row, advanced knob grid.
        history_.setBounds(centre.removeFromTop(120));
        centre.removeFromTop(8);
        auto mainRow = centre.removeFromTop(96);
        auto toggles = mainRow.removeFromRight(110);
        riderToggle_.setBounds(toggles.removeFromTop(mainRow.getHeight() / 2).withSizeKeepingCentre(110, 26));
        guardToggle_.setBounds(toggles.withSizeKeepingCentre(110, 26));
        const int kw = mainRow.getWidth() / 3;
        for (auto* k : { &target_, &staticGain_, &strength_ })
        {
            auto cell = mainRow.removeFromLeft(kw);
            k->label.setBounds(cell.removeFromTop(16));
            k->slider.setBounds(cell);
        }
        centre.removeFromTop(6);
        constexpr int columns = 5;
        const int rows = (static_cast<int>(advancedKnobs_.size()) + columns - 1) / columns;
        const int cellW = centre.getWidth() / columns;
        const int cellH = centre.getHeight() / juce::jmax(1, rows);
        for (size_t i = 0; i < advancedKnobs_.size(); ++i)
        {
            const int col = static_cast<int>(i) % columns, row = static_cast<int>(i) / columns;
            auto cell = juce::Rectangle<int>(centre.getX() + col * cellW, centre.getY() + row * cellH, cellW, cellH).reduced(2);
            advancedKnobs_[i]->label.setBounds(cell.removeFromTop(14));
            advancedKnobs_[i]->slider.setBounds(cell);
            advancedKnobs_[i]->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 15);
        }
    }
}

void MixCoreEditor::timerCallback() { refresh(); }

void MixCoreEditor::refresh()
{
    const bool fresh = processor_.readTelemetry(telemetry_);
    const auto& t = telemetry_;
    const auto settings = processor_.currentSettings();
    constexpr double dt = 1.0 / kTimerHz;

    if (fresh)
    {
        inputMeter_.setLevels(t.inputRmsDb, t.inputPeakDb, dt);
        outputMeter_.setLevels(t.outputRmsDb, t.outputPeakDb, dt);
        history_.push(t.riderGainDb, t.peakGuardReductionDb, static_cast<int>(t.activity));
    }
    inputMeter_.setAlert(t.inputClipping);
    outputMeter_.setAlert(t.peakGuardActive);
    outputMeter_.setTargetDb(settings.targetRmsDb);
    inputMeter_.setWarningDb(settings.peakWarningDb);
    outputMeter_.setWarningDb(settings.peakWarningDb);

    inputReadout_.setText("RMS " + theme::formatDb(t.inputRmsDb, 1, false) + "\npk " + theme::formatDb(inputMeter_.peakHoldDb(), 1, false),
                          juce::dontSendNotification);
    outputReadout_.setText("RMS " + theme::formatDb(t.outputRmsDb, 1, false) + "\nTP " + theme::formatDb(t.outputTruePeakDb, 1, false),
                           juce::dontSendNotification);

    activityLabel_.setText(t.bypassed ? "BYPASSED" : activityText(t.activity), juce::dontSendNotification);
    activityLabel_.setColour(juce::Label::textColourId, t.bypassed ? theme::safety : activityColour(t.activity));

    staticChip_.setText(theme::formatDb(t.staticGainDb) + " dB", juce::dontSendNotification);
    juce::String riderText = t.riderEnabled ? theme::formatDb(t.riderGainDb) + " dB" : "off";
    if (t.peakRestrictionActive)
        riderText << "  (held " << juce::String(t.riderRestrictionDb, 1) << ")";
    riderChip_.setText(riderText, juce::dontSendNotification);
    guardChip_.setText(!t.peakGuardEnabled ? "off" : (t.peakGuardActive ? "-" + juce::String(t.peakGuardReductionDb, 1) + " dB" : "ready"),
                       juce::dontSendNotification);

    updateCalibrationUi();
    if (--coachCountdown_ <= 0)
    {
        coachCountdown_ = kTimerHz / 3; // ~3 Hz is plenty for text
        updateCoach(t);
    }
    repaint(getLocalBounds().removeFromTop(52)); // header sample-rate/latency text
}

void MixCoreEditor::updateCalibrationUi()
{
    const auto state = processor_.calibrationState();
    calibrationProgress_ = static_cast<double>(telemetry_.calibrationProgress);

    if ((state == gain::CalibrationState::Ready || state == gain::CalibrationState::TimedOut)
        && lastCalState_ != state)
    {
        lastResult_ = processor_.computeCalibrationResult();
        calibrationText_.setText(juce::String(lastResult_.summary), false);
        calibrationProgress_ = 1.0;
    }
    else if (state != lastCalState_)
    {
        switch (state)
        {
            case gain::CalibrationState::Requested:
            case gain::CalibrationState::Collecting:
                calibrationText_.setText("Listening... sing or speak at your performance level. "
                                         "Only active vocal is measured; pauses are ignored.",
                                         false);
                break;
            case gain::CalibrationState::Idle:
                if (!lastResult_.valid)
                    calibrationText_.setText("Calibration measures the typical level of your vocal and suggests a static gain "
                                             "that brings it to the target. Nothing changes until you apply it.",
                                             false);
                break;
            case gain::CalibrationState::Ready:
            case gain::CalibrationState::TimedOut:
                break;
        }
    }
    lastCalState_ = state;

    const bool running = state == gain::CalibrationState::Requested || state == gain::CalibrationState::Collecting;
    calibrateButton_.setEnabled(!running);
    calibrateButton_.setButtonText(lastResult_.valid ? "Recalibrate" : "Calibrate (6 s of singing)");
    cancelCalButton_.setEnabled(running || lastResult_.valid);
    applyButton_.setEnabled(lastResult_.valid && !running);
    applySafeButton_.setVisible(lastResult_.valid && lastResult_.peakConflict);
    progressBar_.setVisible(running);
}

void MixCoreEditor::updateCoach(const gain::SmartGainTelemetry& t)
{
    const auto advice = gain::evaluateSmartGainAdvice(t, processor_.currentSettings());

    juce::String key;
    for (const auto& a : advice)
        key << a.id << ";";
    if (key == lastCoachKey_ && !key.containsIgnoreCase("peak-guard") && !key.containsIgnoreCase("below-target"))
        return; // unchanged: avoid resetting the scroll position
    lastCoachKey_ = key;

    coachText_.clear();
    if (advice.empty())
    {
        coachText_.setColour(juce::TextEditor::textColourId, theme::good);
        coachText_.setFont(theme::font(13.0f, true));
        coachText_.insertTextAtCaret("All clear.\n");
        coachText_.setColour(juce::TextEditor::textColourId, theme::muted);
        coachText_.setFont(theme::font(12.5f));
        coachText_.insertTextAtCaret("No gain-staging problems detected. Calibrate once per singer, then let the rider handle "
                                     "phrase-to-phrase changes.");
        return;
    }

    for (const auto& a : advice)
    {
        const auto colour = a.severity == gain::AdviceSeverity::Problem   ? theme::safety
                            : a.severity == gain::AdviceSeverity::Warning ? theme::rider
                                                                          : theme::level;
        coachText_.setColour(juce::TextEditor::textColourId, colour);
        coachText_.setFont(theme::font(13.0f, true));
        coachText_.insertTextAtCaret(juce::String(a.detected) + "\n");
        coachText_.setColour(juce::TextEditor::textColourId, theme::text);
        coachText_.setFont(theme::font(12.5f));
        coachText_.insertTextAtCaret("Why: " + juce::String(a.whyItMatters) + "\n");
        coachText_.insertTextAtCaret("Check: " + juce::String(a.whatToCheck) + "\n");
        coachText_.insertTextAtCaret("Do: " + juce::String(a.recommendation) + "\n");
        coachText_.setColour(juce::TextEditor::textColourId, theme::muted);
        coachText_.insertTextAtCaret((a.changesAudioAutomatically ? "Automatic: " : "Manual: ") + juce::String(a.automaticNote) + "\n\n");
    }
    coachText_.moveCaretToTop(false);
}

} // namespace mixcore::plugin
