#include "PluginEditor.h"

#include "mixcore/strip/ChannelPresets.h"
#include "mixcore/strip/StripCoach.h"

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

    // Header: presets
    presetBox_.setTooltip("Factory presets are starting points per source. Loading a preset never changes Static Gain "
                          "(that is set per singer/mic by calibration).");
    presetBox_.onChange = [this] { onPresetChosen(); };
    addAndMakeVisible(presetBox_);
    savePresetButton_.setTooltip("Save the current settings as your own preset (available in every session and DAW).");
    savePresetButton_.onClick = [this] { promptSavePreset(); };
    addAndMakeVisible(savePresetButton_);
    deletePresetButton_.setTooltip("Delete the selected user preset.");
    deletePresetButton_.onClick = [this] { promptDeletePreset(); };
    addAndMakeVisible(deletePresetButton_);
    rebuildPresetMenu();

    // Header: mode & bypass
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
    outputMeter_.setTooltip("Output level. The gold marker is your target RMS.");

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
    styleChip(debleedChip_, theme::goldLight);
    debleedChip_.setTooltip("Drum de-bleed: high-band bleed reduction (after Smart Gain, before the gate). Click to open the Drums page.");
    styleChip(gateChip_, theme::good);
    gateChip_.setTooltip("Gate: gain reduction applied after Smart Gain. Click to open the Gate page.");
    for (auto* c : { &staticChip_, &riderChip_, &guardChip_, &debleedChip_, &gateChip_ })
        addAndMakeVisible(*c);
    addAndMakeVisible(history_);

    // Module pages
    for (auto* tab : { &gainTab_, &gateTab_, &drumTab_ })
    {
        tab->setClickingTogglesState(true);
        tab->setRadioGroupId(4711);
        tab->setColour(juce::TextButton::buttonOnColourId, theme::gold);
        tab->setColour(juce::TextButton::textColourOnId, theme::background);
        addAndMakeVisible(*tab);
    }
    gainTab_.setTooltip("Smart Gain: static gain, rider and peak safety.");
    gateTab_.setTooltip("Gate / expander: source profiles, threshold learning, key filters.");
    gainTab_.onClick = [this] { if (gainTab_.getToggleState()) setPage(Page::SmartGain); };
    gateTab_.onClick = [this] { if (gateTab_.getToggleState()) setPage(Page::Gate); };
    drumTab_.setTooltip("Drums: de-bleed (reduces cymbal and hi-hat bleed in drum mics, also while the gate is open).");
    drumTab_.onClick = [this] { if (drumTab_.getToggleState()) setPage(Page::Drums); };
    debleedChip_.addMouseListener(this, false);
    addChildComponent(drumPanel_);
    styleReadout(drumInfoText_);
    addChildComponent(drumInfoText_);
    gainTab_.setToggleState(true, juce::dontSendNotification);
    gateChip_.addMouseListener(this, false);
    addChildComponent(gatePanel_);
    addChildComponent(gateLearnPanel_);

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
    gatePanel_.setAdvanced(advanced);
    setPage(page_); // re-applies visibility and layout
}

juce::Rectangle<int> MixCoreEditor::statusTextArea() const
{
    return gainTab_.getBounds().withLeft(drumTab_.getRight() + 8).withRight(activityLabel_.getRight());
}

void MixCoreEditor::setPage(Page page)
{
    page_ = page;
    const bool gainPage = page == Page::SmartGain;
    const bool gatePage = page == Page::Gate;
    const bool drumPage = page == Page::Drums;
    gainTab_.setToggleState(gainPage, juce::dontSendNotification);
    gateTab_.setToggleState(gatePage, juce::dontSendNotification);
    drumTab_.setToggleState(drumPage, juce::dontSendNotification);

    history_.setVisible(gainPage);
    for (auto* k : { &target_, &staticGain_, &strength_ })
    {
        k->slider.setVisible(gainPage);
        k->label.setVisible(gainPage);
    }
    riderToggle_.setVisible(gainPage);
    guardToggle_.setVisible(gainPage);
    for (auto& k : advancedKnobs_)
    {
        k->slider.setVisible(gainPage && advanced_);
        k->label.setVisible(gainPage && advanced_);
    }
    gatePanel_.setVisible(gatePage);
    drumPanel_.setVisible(drumPage);
    drumInfoText_.setVisible(drumPage);
    lastDrumInfo_.clear();

    for (juce::Component* c : { static_cast<juce::Component*>(&calibrateButton_), static_cast<juce::Component*>(&applyButton_),
                                static_cast<juce::Component*>(&cancelCalButton_), static_cast<juce::Component*>(&calibrationText_) })
        c->setVisible(gainPage);
    if (!gainPage)
    {
        applySafeButton_.setVisible(false);
        progressBar_.setVisible(false);
    }
    gateLearnPanel_.setVisible(gatePage);
    if (gainPage)
        updateCalibrationUi();
    resized();
    repaint();
}

void MixCoreEditor::paint(juce::Graphics& g)
{
    // Background: warm black at the top fading to black.
    g.setGradientFill(juce::ColourGradient(theme::backgroundTop, 0.0f, 0.0f, theme::background, 0.0f,
                                           static_cast<float>(getHeight()) * 0.6f, false));
    g.fillAll();

    // Header: VineAudio logo, product name, thin gold rule underneath.
    auto header = getLocalBounds().removeFromTop(52).reduced(18, 0);
    if (const auto logo = theme::logoImage(); logo.isValid())
    {
        const float h = 38.0f;
        const float w = h * static_cast<float>(logo.getWidth()) / static_cast<float>(logo.getHeight());
        g.drawImage(logo, juce::Rectangle<float>(static_cast<float>(header.getX()), static_cast<float>(header.getCentreY()) - h * 0.5f, w, h),
                    juce::RectanglePlacement::centred);
        header.removeFromLeft(static_cast<int>(w) + 14);
    }
    g.setColour(theme::gold.withAlpha(0.5f));
    g.fillRect(header.getX(), header.getCentreY() - 11, 1, 22);
    header.removeFromLeft(12);
    g.setColour(theme::text);
    g.setFont(theme::font(20.0f, true));
    g.drawText("MixCore", header.removeFromLeft(90), juce::Justification::centredLeft);
    {
        juce::ColourGradient rule(theme::gold.withAlpha(0.0f), 0.0f, 52.0f, theme::gold.withAlpha(0.0f),
                                  static_cast<float>(getWidth()), 52.0f, false);
        rule.addColour(0.15, theme::goldShadow);
        rule.addColour(0.5, theme::goldHighlight.withAlpha(0.8f));
        rule.addColour(0.85, theme::goldShadow);
        g.setGradientFill(rule);
        g.fillRect(0, 51, getWidth(), 1);
    }

    if (telemetry_.sampleRate > 0.0)
    {
        g.setFont(theme::font(11.0f));
        const int latency = telemetry_.latencySamples + gateTelemetry_.latencySamples;
        g.drawText(juce::String(telemetry_.sampleRate / 1000.0, 1) + " kHz  |  latency " + juce::String(latency) + " smp",
                   statusTextArea(), juce::Justification::centredRight);
    }

    // Panels
    for (auto r : { juce::Rectangle<int>(12, 60, 150, getHeight() - 72),
                    juce::Rectangle<int>(getWidth() - 352, 60, 340, getHeight() - 72) })
        theme::fillPanel(g, r.toFloat());

    g.setColour(theme::muted);
    g.setFont(theme::font(11.0f, true));
    g.drawText("STATIC", staticChip_.getBounds().translated(0, -16).withHeight(14), juce::Justification::centred);
    g.drawText("RIDER", riderChip_.getBounds().translated(0, -16).withHeight(14), juce::Justification::centred);
    g.drawText("PEAK GUARD", guardChip_.getBounds().translated(0, -16).withHeight(14), juce::Justification::centred);
    g.drawText("DE-BLEED", debleedChip_.getBounds().translated(0, -16).withHeight(14), juce::Justification::centred);
    g.drawText("GATE", gateChip_.getBounds().translated(0, -16).withHeight(14), juce::Justification::centred);
    g.drawText(page_ == Page::Gate ? "GATE THRESHOLD" : page_ == Page::Drums ? "DE-BLEED" : "CALIBRATION", juce::Rectangle<int>(getWidth() - 340, 66, 200, 16),
               juce::Justification::centredLeft);
    g.drawText("MIX COACH", coachText_.getBounds().translated(0, -20).withHeight(16), juce::Justification::centredLeft);

    // Signal-flow arrows between the chips.
    g.setColour(theme::muted);
    for (auto* c : { &staticChip_, &riderChip_, &guardChip_, &debleedChip_ })
    {
        const auto r = c->getBounds();
        g.drawArrow(juce::Line<float>(static_cast<float>(r.getRight() + 4), static_cast<float>(r.getCentreY()),
                                      static_cast<float>(r.getRight() + 14), static_cast<float>(r.getCentreY())),
                    1.5f, 6.0f, 6.0f);
    }
}

void MixCoreEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(52).reduced(18, 12);
    bypassButton_.setBounds(header.removeFromRight(84));
    header.removeFromRight(8);
    advancedButton_.setBounds(header.removeFromRight(96));
    header.removeFromLeft(262); // logo + product name
    presetBox_.setBounds(header.removeFromLeft(juce::jmin(300, header.getWidth() - 160)));
    header.removeFromLeft(6);
    savePresetButton_.setBounds(header.removeFromLeft(62));
    header.removeFromLeft(6);
    deletePresetButton_.setBounds(header.removeFromLeft(70));

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
    gateLearnPanel_.setBounds(cal);
    drumInfoText_.setBounds(cal);
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
    auto tabs = centre.removeFromTop(26);
    gainTab_.setBounds(tabs.removeFromLeft(110));
    tabs.removeFromLeft(4);
    gateTab_.setBounds(tabs.removeFromLeft(80));
    tabs.removeFromLeft(4);
    drumTab_.setBounds(tabs.removeFromLeft(80));
    centre.removeFromTop(4);
    activityLabel_.setBounds(centre.removeFromTop(20));
    centre.removeFromTop(16);
    auto chips = centre.removeFromTop(36);
    constexpr int gap = 18;
    const int chipW = (chips.getWidth() - 4 * gap) / 5;
    for (auto* chip : { &staticChip_, &riderChip_, &guardChip_, &debleedChip_ })
    {
        chip->setBounds(chips.removeFromLeft(chipW));
        chips.removeFromLeft(gap);
    }
    gateChip_.setBounds(chips);
    centre.removeFromTop(10);

    gatePanel_.setBounds(centre);
    drumPanel_.setBounds(centre);

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
        history_.setBounds(centre.removeFromTop(80));
        centre.removeFromTop(6);
        auto mainRow = centre.removeFromTop(80);
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
    const bool gateFresh = processor_.readGateTelemetry(gateTelemetry_);
    const auto& t = telemetry_;
    const auto& gt = gateTelemetry_;
    const auto settings = processor_.currentSettings();
    constexpr double dt = 1.0 / kTimerHz;

    if (fresh)
    {
        inputMeter_.setLevels(t.inputRmsDb, t.inputPeakDb, dt);
        history_.push(t.riderGainDb, t.peakGuardReductionDb, static_cast<int>(t.activity));
    }
    if (gateFresh)
        outputMeter_.setLevels(gt.outputRmsDb, gt.outputPeakDb, dt); // channel output (after the gate)
    gatePanel_.update(gt, gateFresh);
    const bool drumFresh = processor_.readDebleedTelemetry(drumTelemetry_);
    const auto& drumT = drumTelemetry_;
    drumPanel_.update(drumT, drumFresh);
    if (!drumT.enabled)
    {
        debleedChip_.setText("off", juce::dontSendNotification);
        debleedChip_.setColour(juce::Label::textColourId, theme::muted);
    }
    else
    {
        debleedChip_.setText(drumT.reductionDb < 0.5f ? "clean" : "-" + juce::String(drumT.reductionDb, 1) + " dB",
                             juce::dontSendNotification);
        debleedChip_.setColour(juce::Label::textColourId, theme::goldLight);
    }
    if (page_ == Page::Gate)
        gateLearnPanel_.update(gt.learnProgress);

    if (!gt.enabled)
    {
        gateChip_.setText("off", juce::dontSendNotification);
        gateChip_.setColour(juce::Label::textColourId, theme::muted);
    }
    else
    {
        const bool open = gt.state != gate::GateState::Closed;
        gateChip_.setText(open && gt.gainReductionDb < 0.5f ? "open" : "-" + juce::String(gt.gainReductionDb, 1) + " dB",
                          juce::dontSendNotification);
        gateChip_.setColour(juce::Label::textColourId, open ? theme::good : theme::safety);
    }
    inputMeter_.setAlert(t.inputClipping);
    outputMeter_.setAlert(t.peakGuardActive);
    outputMeter_.setTargetDb(settings.targetRmsDb);
    inputMeter_.setWarningDb(settings.peakWarningDb);
    outputMeter_.setWarningDb(settings.peakWarningDb);

    inputReadout_.setText("RMS " + theme::formatDb(t.inputRmsDb, 1, false) + "\npk " + theme::formatDb(inputMeter_.peakHoldDb(), 1, false),
                          juce::dontSendNotification);
    outputReadout_.setText("RMS " + theme::formatDb(gt.outputRmsDb, 1, false) + "\nTP " + theme::formatDb(t.outputTruePeakDb, 1, false),
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

    if (page_ == Page::SmartGain)
        updateCalibrationUi();
    if (--coachCountdown_ <= 0)
    {
        coachCountdown_ = kTimerHz / 3; // ~3 Hz is plenty for text
        updateCoach(t);
        updatePresetDisplay();
        if (page_ == Page::Drums)
        {
            const auto info = drumPanel_.statusText(drumT);
            if (info != lastDrumInfo_)
            {
                lastDrumInfo_ = info;
                drumInfoText_.setText(info, false);
            }
        }
    }
    repaint(statusTextArea()); // sample-rate/latency text
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
    const auto advice = strip::evaluateStripAdvice(t, gateTelemetry_, processor_.currentChannelSettings());

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

// ------------------------------------------------------------------ Presets

void MixCoreEditor::rebuildPresetMenu()
{
    presetBox_.clear(juce::dontSendNotification);

    std::string lastCategory;
    const auto& factory = strip::channelPresets();
    for (size_t i = 0; i < factory.size(); ++i)
    {
        if (factory[i].category != lastCategory)
        {
            if (!lastCategory.empty())
                presetBox_.addSeparator();
            presetBox_.addSectionHeading(juce::String(factory[i].category));
            lastCategory = factory[i].category;
        }
        presetBox_.addItem(juce::String(factory[i].name), static_cast<int>(i) + 1);
    }

    userPresetNames_.clear();
    const auto user = processor_.presetManager().userPresets();
    presetBox_.addSeparator();
    presetBox_.addSectionHeading("My Presets");
    if (user.isEmpty())
    {
        presetBox_.addItem("(none yet - press Save)", kUserIdBase - 1);
        presetBox_.setItemEnabled(kUserIdBase - 1, false);
    }
    for (int i = 0; i < user.size(); ++i)
    {
        userPresetNames_.add(user[i].name);
        presetBox_.addItem(user[i].name, kUserIdBase + i);
    }
    lastPresetDisplay_.clear();
    updatePresetDisplay();
}

void MixCoreEditor::updatePresetDisplay()
{
    const auto name = processor_.currentPresetName();
    const bool isUser = processor_.currentPresetIsUser();
    const bool modified = processor_.isPresetModified();

    const auto display = name + (modified ? "  *" : "");
    if (display == lastPresetDisplay_)
        return;
    lastPresetDisplay_ = display;

    int id = 0;
    if (isUser)
    {
        const int idx = userPresetNames_.indexOf(name);
        id = idx >= 0 ? kUserIdBase + idx : 0;
    }
    else
    {
        const auto& factory = strip::channelPresets();
        for (size_t i = 0; i < factory.size(); ++i)
            if (juce::String(factory[i].name) == name)
                id = static_cast<int>(i) + 1;
    }

    if (modified || id == 0)
        presetBox_.setText(display, juce::dontSendNotification); // shows "Name  *" without selecting an item
    else
        presetBox_.setSelectedId(id, juce::dontSendNotification);

    deletePresetButton_.setEnabled(isUser && userPresetNames_.contains(name));
    presetBox_.setTooltip(modified ? "Settings changed since this preset was loaded (*). Save to keep them as your own preset."
                                   : "Factory presets are starting points per source. Loading a preset never changes Static Gain.");
}

void MixCoreEditor::onPresetChosen()
{
    const int id = presetBox_.getSelectedId();
    if (id <= 0)
        return;
    if (id >= kUserIdBase)
    {
        const int idx = id - kUserIdBase;
        if (idx < userPresetNames_.size() && !processor_.loadUserPreset(userPresetNames_[idx]))
        {
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                                             .withTitle("Preset could not be loaded")
                                             .withMessage("The preset file is missing or damaged.")
                                             .withButton("OK"),
                                         nullptr);
            rebuildPresetMenu();
        }
    }
    else
    {
        const auto& factory = strip::channelPresets();
        if (id - 1 < static_cast<int>(factory.size()))
            processor_.loadFactoryPreset(juce::String(factory[static_cast<size_t>(id - 1)].name));
    }
    lastPresetDisplay_.clear();
    updatePresetDisplay();
}

void MixCoreEditor::promptSavePreset()
{
    auto* window = new juce::AlertWindow("Save preset",
                                         "Name your preset. It will appear under \"My Presets\" in every session and DAW.\n"
                                         "Static gain is stored too, but loading a preset never changes static gain.",
                                         juce::MessageBoxIconType::NoIcon, this);
    const auto suggestion = processor_.currentPresetIsUser() ? processor_.currentPresetName()
                                                              : processor_.currentPresetName() + " (mine)";
    window->addTextEditor("name", suggestion, "Preset name:");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MixCoreEditor> safe(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window](int result) {
                                if (result != 1 || safe == nullptr)
                                    return;
                                const auto name = PresetManager::sanitiseName(window->getTextEditorContents("name"));
                                auto doSave = [safe, name] {
                                    if (safe == nullptr)
                                        return;
                                    const auto error = safe->processor_.saveUserPreset(name);
                                    if (error.isNotEmpty())
                                        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                                                         .withTitle("Preset not saved")
                                                                         .withMessage(error)
                                                                         .withButton("OK"),
                                                                     nullptr);
                                    safe->rebuildPresetMenu();
                                };
                                if (safe->processor_.presetManager().exists(name))
                                    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                                                     .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                                                     .withTitle("Replace preset?")
                                                                     .withMessage("A preset called \"" + name + "\" already exists.")
                                                                     .withButton("Replace")
                                                                     .withButton("Cancel"),
                                                                 [doSave](int r) {
                                                                     if (r == 1)
                                                                         doSave();
                                                                 });
                                else
                                    doSave();
                            }),
                            true);
}

void MixCoreEditor::promptDeletePreset()
{
    if (!processor_.currentPresetIsUser())
        return;
    const auto name = processor_.currentPresetName();
    juce::Component::SafePointer<MixCoreEditor> safe(this);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle("Delete preset?")
                                     .withMessage("Delete \"" + name + "\"? This removes the preset file and cannot be undone. "
                                                  "Your current settings stay as they are.")
                                     .withButton("Delete")
                                     .withButton("Cancel"),
                                 [safe, name](int r) {
                                     if (r == 1 && safe != nullptr)
                                     {
                                         safe->processor_.deleteUserPreset(name);
                                         safe->rebuildPresetMenu();
                                     }
                                 });
}

} // namespace mixcore::plugin
