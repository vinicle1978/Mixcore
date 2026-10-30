#include "ReplacePanel.h"

#include "mixcore/drum/StarterKits.h"

namespace mixcore::plugin {

namespace {
constexpr int kLoadFilesId = 100;
constexpr int kLoadFolderId = 101;
constexpr float kLiveMs = 1.5f;
constexpr float kStudioMs = 6.0f;
} // namespace

// ------------------------------------------------------------- ReplaceHistory

void ReplaceHistory::push(float detectorDb, float thresholdDb, int hits, float velocity, bool enabled)
{
    const auto i = static_cast<size_t>(pos_);
    det_[i] = detectorDb;
    vel_[i] = hits > 0 ? juce::jmax(0.05f, velocity) : 0.0f;
    thr_ = thresholdDb;
    enabled_ = enabled;
    pos_ = (pos_ + 1) % kPoints;
    count_ = juce::jmin(count_ + 1, kPoints);
    repaint();
}

void ReplaceHistory::paint(juce::Graphics& g)
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
        juce::Path det;
        for (int i = 0; i < count_; ++i)
        {
            const auto idx = static_cast<size_t>((pos_ - count_ + i + kPoints) % kPoints);
            const float x = plot.getRight() - static_cast<float>(count_ - 1 - i) * dx;
            if (i == 0)
                det.startNewSubPath(x, yFor(det_[idx]));
            det.lineTo(x, yFor(det_[idx]));
            if (vel_[idx] > 0.0f)
            {
                // Hit marker: height = velocity (bottom 40 % of the plot).
                const float h = vel_[idx] * plot.getHeight() * 0.4f;
                auto bar = juce::Rectangle<float>(x - 1.5f, plot.getBottom() - h, 3.0f, h);
                g.setGradientFill(theme::silverGradient(bar));
                g.fillRect(bar);
            }
        }
        g.setColour(theme::level);
        g.strokePath(det, juce::PathStrokeType(1.5f));
    }
    g.setColour(theme::rider);
    g.drawHorizontalLine(static_cast<int>(yFor(thr_)), plot.getX(), plot.getRight());
    g.setFont(theme::font(9.5f, true));
    g.drawText("threshold " + juce::String(thr_, 1), plot.withHeight(12.0f).withY(yFor(thr_) - 13.0f).withTrimmedRight(4.0f),
               juce::Justification::centredRight);
    g.setColour(theme::muted);
    g.setFont(theme::font(9.5f));
    g.drawText(enabled_ ? "detector level, threshold, hits (bar height = velocity)" : "REPLACE OFF - showing detector level only",
               plot.withHeight(14.0f), juce::Justification::topLeft);
}

// --------------------------------------------------------------- ReplacePanel

ReplacePanel::ReplacePanel(MixCoreProcessor& processor) : processor_(processor)
{
    auto& apvts = processor_.state();
    addAndMakeVisible(history_);

    rebuildKitMenu();
    kitBox_.setTooltip("Choose a starter kit (original MixCore sounds) or load your own WAV/AIFF samples. MixCore sorts "
                       "your samples by loudness into velocity layers; similar-loudness samples become round-robin "
                       "variations.");
    kitBox_.onChange = [this] { onKitChosen(); };
    addAndMakeVisible(kitBox_);

    for (auto* b : { &liveButton_, &studioButton_ })
    {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(9411);
        addAndMakeVisible(*b);
    }
    liveButton_.setTooltip("Live: 1.5 ms detection window (added latency). Safe for in-ears and monitors.");
    studioButton_.setTooltip("Studio: 6 ms detection window. More accurate velocity on slow attacks (kick), more latency "
                             "(your DAW compensates it).");
    liveButton_.onClick = [this] { if (liveButton_.getToggleState()) setLookahead(kLiveMs); };
    studioButton_.onClick = [this] { if (studioButton_.getToggleState()) setLookahead(kStudioMs); };

    onButton_.setClickingTogglesState(true);
    onButton_.setTooltip("Switch replacement on or off. Off = no hits are detected and no samples play; the drum passes "
                         "through untouched with no added latency.");
    onAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ids::drumReplace, onButton_);
    addAndMakeVisible(onButton_);

    kitInfo_.setFont(theme::font(12.0f, true));
    kitInfo_.setColour(juce::Label::textColourId, theme::label);
    addAndMakeVisible(kitInfo_);

    addKnob(ids::replaceThreshold, "Threshold", theme::rider,
            "Detector level that counts as a hit. Every real hit should show a marker in the graph; bleed should not.");
    addKnob(ids::replaceRetrigger, "Retrigger", theme::level,
            "Minimum time between two hits. Raise it if one hit triggers twice; lower it for fast rolls.");
    addKnob(ids::replaceDynamics, "Dynamics", theme::level,
            "How much of the drummer's dynamics the samples follow. 100 % = soft hits play soft samples; 0 % = every "
            "hit at full velocity.");
    addKnob(ids::replaceDetectLow, "Detect Low", theme::level,
            "The detector ignores content below this (e.g. 150 Hz on snare ignores the kick).");
    addKnob(ids::replaceDetectHigh, "Detect High", theme::level,
            "The detector ignores content above this (e.g. 200 Hz on kick ignores snare and cymbals).");
    addKnob(ids::replaceSample, "Sample", theme::blueLight, "Level of the triggered samples.");
    addKnob(ids::replaceOriginal, "Original", theme::silver,
            "Level of the original drum. Off = pure replacement; equal levels = augmentation (blend).");
    addKnob(ids::replacePitch, "Pitch", theme::level, "Tune the samples in semitones.");
    addKnob(ids::replaceAttack, "Attack", theme::level,
            "Fades in the start of each sample. 0 = full transient; a few ms softens a hard click.");
    addKnob(ids::replaceLength, "Length", theme::level,
            "Shortens the samples (they fade out over the last part). Full = whole sample.");
    refreshKitInfo();
}

void ReplacePanel::addKnob(const char* id, const juce::String& caption, juce::Colour accent, const juce::String& tooltip)
{
    auto knob = std::make_unique<Knob>();
    auto& k = *knob;
    knobs_.push_back(std::move(knob));
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 15);
    k.slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setTooltip(tooltip);
    k.label.setText(caption, juce::dontSendNotification);
    k.label.setFont(theme::font(12.0f, true));
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, theme::label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor_.state(), id, k.slider);
    addAndMakeVisible(k.slider);
    addAndMakeVisible(k.label);
}

void ReplacePanel::rebuildKitMenu()
{
    kitBox_.clear(juce::dontSendNotification);
    kitBox_.addSectionHeading("Starter kits (MixCore originals)");
    const auto& kits = drum::starterKits();
    for (size_t i = 0; i < kits.size(); ++i)
        kitBox_.addItem(kits[i].name, static_cast<int>(i) + 1);
    kitBox_.addSeparator();
    kitBox_.addSectionHeading("Your samples");
    kitBox_.addItem("Load samples (WAV/AIFF)...", kLoadFilesId);
    kitBox_.addItem("Load kit folder...", kLoadFolderId);
}

void ReplacePanel::setLookahead(float ms)
{
    if (auto* p = processor_.state().getParameter(ids::replaceLookahead))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(ms));
        p->endChangeGesture();
    }
}

void ReplacePanel::showMessage(const juce::String& text)
{
    message_ = text;
    refreshKitInfo();
}

void ReplacePanel::onKitChosen()
{
    const int id = kitBox_.getSelectedId();
    if (id <= 0)
        return;
    const auto& kits = drum::starterKits();
    if (id <= static_cast<int>(kits.size()))
    {
        showMessage(processor_.loadStarterKit(kits[static_cast<size_t>(id - 1)].id));
        return;
    }
    const bool folder = id == kLoadFolderId;
    chooser_ = std::make_unique<juce::FileChooser>(folder ? "Choose a folder of drum samples" : "Choose drum samples",
                                                   juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                                   folder ? juce::String() : juce::String("*.wav;*.aif;*.aiff;*.flac"));
    const int browseFlags = juce::FileBrowserComponent::openMode
                      | (folder ? juce::FileBrowserComponent::canSelectDirectories
                                : juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems);
    juce::Component::SafePointer<ReplacePanel> safe(this);
    chooser_->launchAsync(browseFlags, [safe, folder](const juce::FileChooser& fc) {
        if (safe == nullptr)
            return;
        const auto results = fc.getResults();
        if (!results.isEmpty())
            safe->showMessage(folder ? safe->processor_.loadKitFolder(results[0]) : safe->processor_.loadKitFiles(results));
        safe->refreshKitInfo();
    });
}

void ReplacePanel::refreshKitInfo()
{
    const auto source = processor_.kitSource();
    lastKitSource_ = source;
    // Show the current kit in the menu.
    int selected = 0;
    const auto& kits = drum::starterKits();
    for (size_t i = 0; i < kits.size(); ++i)
        if (source == kits[i].id)
            selected = static_cast<int>(i) + 1;
    if (selected > 0)
        kitBox_.setSelectedId(selected, juce::dontSendNotification);
    else
        kitBox_.setText(source.isEmpty() ? juce::String("Choose a kit...") : processor_.kitName(), juce::dontSendNotification);

    juce::String info = processor_.kitDescription();
    if (message_.isNotEmpty())
        info << "   (" << message_ << ")";
    kitInfo_.setText(info, juce::dontSendNotification);
    kitInfo_.setColour(juce::Label::textColourId, message_.isNotEmpty() ? theme::warning : theme::label);
}

void ReplacePanel::update(const drum::ReplaceTelemetry& t, bool fresh)
{
    if (fresh)
        history_.push(t.detectorDb, t.thresholdDb, t.hitsThisTick, t.lastVelocity, t.enabled);
    onButton_.setButtonText(onButton_.getToggleState() ? "Replace ON" : "Replace OFF");
    const float la = processor_.currentChannelSettings().drum.replaceLookaheadMs;
    liveButton_.setToggleState(la < 3.0f, juce::dontSendNotification);
    studioButton_.setToggleState(la >= 3.0f, juce::dontSendNotification);
    if (processor_.kitSource() != lastKitSource_)
    {
        message_.clear();
        refreshKitInfo();
    }
    const bool on = onButton_.getToggleState();
    for (auto& k : knobs_)
    {
        k->slider.setAlpha(on ? 1.0f : 0.5f);
        k->label.setAlpha(on ? 1.0f : 0.5f);
    }
}

juce::String ReplacePanel::statusText(const drum::ReplaceTelemetry& t) const
{
    const auto d = processor_.currentChannelSettings().drum;
    juce::String s;
    if (!t.enabled)
        s << "Replacement is off.\n\n";
    else if (!t.kitLoaded)
        s << "Replacement is on, but no kit is loaded. Choose a starter kit or load your samples.\n\n";
    else
        s << "Working. Hits in the last second: " << t.hitsLastSecond << ", last velocity "
          << juce::String(juce::roundToInt(t.lastVelocity * 100.0f)) << " %.\n"
          << "Added latency: " << juce::String(d.replaceLookaheadMs, 1) << " ms ("
          << (d.replaceLookaheadMs < 3.0f ? "Live" : "Studio") << ").\n\n";
    s << "How it works\n"
         "MixCore listens to the drum mic (before the gate), detects each hit and its strength, and plays a sample from "
         "the kit - softer hits pick softer samples (velocity layers), repeated hits alternate between variations (round "
         "robin). The sample starts exactly on the hit, so it stays in phase with the original.\n\n"
         "Blend with Original: Off = pure replacement, both up = augmentation.\n\n"
         "Your own samples\n"
         "Load several WAV/AIFF files of one drum at different strengths (or a kit folder). MixCore sorts them by "
         "loudness into layers automatically.\n\n"
         "Limits\n"
         "In Live mode the strength is measured over 1.5 ms, which is enough for snare and toms; kicks with slow attacks "
         "follow the dynamics better in Studio mode. Check by ear that ghost notes trigger and bleed does not.";
    return s;
}

void ReplacePanel::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(28);
    onButton_.setBounds(top.removeFromRight(120));
    top.removeFromRight(8);
    studioButton_.setBounds(top.removeFromRight(60));
    liveButton_.setBounds(top.removeFromRight(50));
    top.removeFromRight(8);
    kitBox_.setBounds(top);
    area.removeFromTop(4);
    kitInfo_.setBounds(area.removeFromTop(20));
    area.removeFromTop(4);

    auto knobs = area.removeFromBottom(178);
    history_.setBounds(area.withTrimmedBottom(6));
    const int perRow = 5;
    const int rowH = knobs.getHeight() / 2;
    for (size_t i = 0; i < knobs_.size(); ++i)
    {
        const int row = static_cast<int>(i) / perRow, col = static_cast<int>(i) % perRow;
        const int cw = knobs.getWidth() / perRow;
        auto cell = juce::Rectangle<int>(knobs.getX() + col * cw, knobs.getY() + row * rowH, cw, rowH).reduced(2, 1);
        knobs_[i]->label.setBounds(cell.removeFromTop(15));
        knobs_[i]->slider.setBounds(cell);
    }
}

} // namespace mixcore::plugin
