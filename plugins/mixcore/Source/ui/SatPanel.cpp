#include "SatPanel.h"

#include "mixcore/sat/SatCurves.h"
#include "mixcore/sat/SatProfiles.h"

#include <cmath>

namespace mixcore::plugin {

namespace {
const char* kTypeTips[sat::kNumSatTypes] = {
    "Tape: smooth, symmetric rounding of the peaks (odd harmonics). Warm and forgiving.",
    "Tube: asymmetric, adds even harmonics (2nd, 4th). Thick and warm; good on kick, bass and vocals.",
    "Console: very gentle until it flattens. Subtle density and glue.",
    "Clip: hard clipping. Bright and aggressive, flat tops; best blended or used lightly on drums.",
};

juce::String percentText(float db)
{
    const double pct = 100.0 * std::pow(10.0, static_cast<double>(db) / 20.0);
    return pct < 0.1 ? juce::String("< 0.1 %") : (pct < 10.0 ? juce::String(pct, 1) : juce::String(juce::roundToInt(pct))) + " %";
}
} // namespace

// ------------------------------------------------------------------ SatCurve

SatCurve::SatCurve(MixCoreProcessor& p) : processor_(p)
{
    setTooltip("The saturation curve with the current drive (input across, output up). The straight line is no saturation; "
               "the markers show how far the loudest peaks reach right now.");
}

void SatCurve::setLivePeak(float peakDb, bool active)
{
    peak_ = dbToGain(peakDb);
    active_ = active;
    repaint();
}

void SatCurve::paint(juce::Graphics& g)
{
    theme::fillPanel(g, getLocalBounds().toFloat(), 6.0f, 0.15f);
    const auto p = getLocalBounds().toFloat().reduced(10.0f);
    const auto s = processor_.currentChannelSettings().sat;
    constexpr float range = 1.5f;
    const auto px = [&](float v) { return p.getCentreX() + v / range * p.getWidth() * 0.5f; };
    const auto py = [&](float v) { return p.getCentreY() - juce::jlimit(-range, range, v) / range * p.getHeight() * 0.5f; };

    g.setColour(theme::panelEdge.withAlpha(0.6f));
    for (float v : { -1.0f, 0.0f, 1.0f })
    {
        g.drawVerticalLine(juce::roundToInt(px(v)), p.getY(), p.getBottom());
        g.drawHorizontalLine(juce::roundToInt(py(v)), p.getX(), p.getRight());
    }
    g.setColour(theme::silver.withAlpha(0.25f));
    g.drawLine(px(-range), py(-range), px(range), py(range), 1.0f);

    const float d = dbToGain(s.driveDb);
    juce::Path curve;
    for (int i = 0; i <= 300; ++i)
    {
        const float x = -range + 2.0f * range * static_cast<float>(i) / 300.0f;
        const juce::Point<float> pt(px(x), py(sat::shape(s.type, x * d)));
        if (i == 0)
            curve.startNewSubPath(pt);
        else
            curve.lineTo(pt);
    }
    g.setColour(s.enabled ? theme::text : theme::muted.withAlpha(0.6f));
    g.strokePath(curve, juce::PathStrokeType(2.2f));

    if (peak_ > 1e-4f)
    {
        const auto col = active_ ? (sat::shape(s.type, peak_ * d) < 0.9f * peak_ * d ? theme::warning : theme::blueLight) : theme::muted;
        for (float sign : { -1.0f, 1.0f })
        {
            const float x = sign * juce::jmin(peak_, range);
            const auto c = juce::Point<float>(px(x), py(sat::shape(s.type, x * d)));
            g.setColour(col.withAlpha(0.35f));
            g.fillEllipse(juce::Rectangle<float>(14.0f, 14.0f).withCentre(c));
            g.setColour(col);
            g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(c));
        }
    }
    g.setColour(theme::muted);
    g.setFont(theme::font(10.0f, true));
    g.drawText(juce::String(sat::satTypeName(s.type)) + "  +" + juce::String(s.driveDb, 1) + " dB", p.reduced(4.0f).removeFromTop(14.0f),
               juce::Justification::topLeft);
}

// -------------------------------------------------------------- HarmonicsView

HarmonicsView::HarmonicsView()
{
    setTooltip("Harmonics added to a tone at the loudest level right now (2nd to 8th), relative to the tone itself. "
               "Even harmonics (2, 4, 6, 8) sound warm and full; odd ones (3, 5, 7) sound brighter and edgier.");
}

void HarmonicsView::set(const std::array<float, 7>& db, float thdDb, bool active)
{
    db_ = db;
    thd_ = thdDb;
    active_ = active;
    repaint();
}

void HarmonicsView::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    theme::fillPanel(g, area, 6.0f, 0.15f);
    area.reduce(8.0f, 6.0f);
    auto title = area.removeFromTop(18.0f);
    g.setFont(theme::font(11.0f, true));
    g.setColour(theme::label);
    g.drawText("HARMONICS", title, juce::Justification::centredLeft);
    g.setColour(active_ ? (thd_ > -20.0f ? theme::warning : theme::ice) : theme::muted);
    g.drawText(thd_ <= -120.0f ? juce::String("clean") : "total " + percentText(thd_), title, juce::Justification::centredRight);
    auto labels = area.removeFromBottom(14.0f);
    area.removeFromLeft(24.0f);
    constexpr float floorDb = -80.0f;
    const auto yFor = [&](float db) { return area.getBottom() - juce::jlimit(0.0f, 1.0f, (db - floorDb) / -floorDb) * area.getHeight(); };
    g.setFont(theme::font(9.0f));
    for (int db : { 0, -20, -40, -60 })
    {
        g.setColour(theme::panelEdge.withAlpha(0.6f));
        g.drawHorizontalLine(juce::roundToInt(yFor(static_cast<float>(db))), area.getX(), area.getRight());
        g.setColour(theme::muted);
        g.drawText(juce::String(db), juce::Rectangle<float>(area.getX() - 24.0f, yFor(static_cast<float>(db)) - 6.0f, 21.0f, 12.0f),
                   juce::Justification::centredRight);
    }
    const float bw = area.getWidth() / 7.0f;
    for (int h = 0; h < 7; ++h)
    {
        const auto col = juce::Rectangle<float>(area.getX() + bw * static_cast<float>(h), area.getY(), bw, area.getHeight()).reduced(bw * 0.18f, 0.0f);
        const float top = yFor(db_[static_cast<size_t>(h)]);
        if (top < col.getBottom() - 0.5f)
        {
            const auto bar = col.withTop(top);
            const bool even = h % 2 == 0; // index 0 = 2nd harmonic
            g.setGradientFill(even ? theme::blueGradient(bar) : theme::silverGradient(bar));
            g.setOpacity(active_ ? 1.0f : 0.45f);
            g.fillRoundedRectangle(bar, 2.0f);
            g.setOpacity(1.0f);
        }
        g.setColour(theme::muted);
        g.drawText(juce::String(h + 2), juce::Rectangle<float>(col.getX(), labels.getY(), col.getWidth(), labels.getHeight()),
                   juce::Justification::centred);
    }
}

// ------------------------------------------------------------------ SatPanel

SatPanel::SatPanel(MixCoreProcessor& processor) : processor_(processor), curve_(processor)
{
    addAndMakeVisible(curve_);
    addAndMakeVisible(harmonics_);

    profileBox_.addSectionHeading("Saturation profiles (starting points)");
    const auto& profiles = sat::satProfiles();
    for (size_t i = 0; i < profiles.size(); ++i)
        profileBox_.addItem(juce::String(profiles[i].name), static_cast<int>(i) + 1);
    profileBox_.setText("Load sat profile...", juce::dontSendNotification);
    profileBox_.setTooltip("Load starting settings for this source (switches saturation on; keeps your Live/Studio choice).");
    profileBox_.onChange = [this] {
        const int id = profileBox_.getSelectedId();
        const auto& list = sat::satProfiles();
        if (id > 0 && id <= static_cast<int>(list.size()))
            processor_.loadSatProfile(juce::String(list[static_cast<size_t>(id - 1)].name));
        profileBox_.setSelectedId(0, juce::dontSendNotification);
        profileBox_.setText("Load sat profile...", juce::dontSendNotification);
    };
    addAndMakeVisible(profileBox_);

    onButton_.setClickingTogglesState(true);
    onButton_.setTooltip("Switch saturation on or off (10 ms crossfade). Off = not processing.");
    onAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor_.state(), ids::satOn, onButton_);
    addAndMakeVisible(onButton_);

    for (int t = 0; t < sat::kNumSatTypes; ++t)
    {
        auto& b = typeButtons_[static_cast<size_t>(t)];
        b.setButtonText(sat::satTypeName(static_cast<sat::SatType>(t)));
        b.setClickingTogglesState(true);
        b.setRadioGroupId(6611);
        b.setTooltip(kTypeTips[t]);
        b.setColour(juce::TextButton::buttonOnColourId, theme::blue);
        b.onClick = [this, t] {
            if (typeButtons_[static_cast<size_t>(t)].getToggleState())
                setChoice(ids::satType, t);
        };
        addAndMakeVisible(b);
    }
    for (auto* b : { &liveButton_, &studioButton_ })
    {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(6612);
        b->setColour(juce::TextButton::buttonOnColourId, theme::blueDeep);
        addAndMakeVisible(*b);
    }
    liveButton_.setTooltip("Live: zero latency. Heavy drive on bright sounds can add some aliasing (harsh, unrelated tones).");
    studioButton_.setTooltip("Studio: 4x oversampling, cleaner at heavy drive. Adds 28 samples (about 0.6 ms at 48 kHz) of latency, "
                             "reported to the host.");
    liveButton_.onClick = [this] { if (liveButton_.getToggleState()) setChoice(ids::satQuality, 0); };
    studioButton_.onClick = [this] { if (studioButton_.getToggleState()) setChoice(ids::satQuality, 1); };
    qualityLabel_.setFont(theme::font(10.5f));
    qualityLabel_.setColour(juce::Label::textColourId, theme::muted);
    addAndMakeVisible(qualityLabel_);

    setupKnob(drive_, ids::satDrive, "Drive", theme::blueLight, "How hard the signal is pushed into the curve. More drive = more harmonics.");
    setupKnob(mix_, ids::satMix, "Mix", theme::level, "Parallel saturation: lower values blend the clean signal back in (no phasing).");
    setupKnob(output_, ids::satOutput, "Output", theme::silver, "Level after saturation.");
    autoGain_.setTooltip("Keeps the loudness about the same as you change the drive, so you judge the colour, not the volume.");
    autoGainAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor_.state(), ids::satAutoGain, autoGain_);
    addAndMakeVisible(autoGain_);
}

void SatPanel::setupKnob(Knob& k, const char* paramId, const juce::String& caption, juce::Colour accent, const juce::String& tip)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 15);
    k.slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setTooltip(tip);
    k.label.setText(caption, juce::dontSendNotification);
    k.label.setFont(theme::font(11.5f, true));
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, theme::label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor_.state(), paramId, k.slider);
    addAndMakeVisible(k.slider);
    addAndMakeVisible(k.label);
}

void SatPanel::setChoice(const char* id, int index)
{
    if (auto* p = processor_.state().getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(index)));
        p->endChangeGesture();
    }
}

void SatPanel::update(const sat::SatTelemetry& t, bool fresh)
{
    const auto s = processor_.currentChannelSettings().sat;
    if (fresh)
    {
        // Peak hold (1 s) so the markers and harmonics show the loud parts, not every dip.
        if (t.inputPeakDb >= peakHoldDb_ || ++peakAge_ > 100)
        {
            peakHoldDb_ = t.inputPeakDb;
            peakAge_ = 0;
        }
        curve_.setLivePeak(peakHoldDb_, s.enabled);
    }
    if (--harmonicsCountdown_ <= 0)
    {
        harmonicsCountdown_ = 3; // ~10 Hz
        const float level = peakHoldDb_ > -80.0f ? peakHoldDb_ : -12.0f;
        harmonics_.set(sat::harmonicsDb(s.type, level, s.driveDb), sat::thdDb(s.type, level, s.driveDb), s.enabled);
    }
    for (int i = 0; i < sat::kNumSatTypes; ++i)
        typeButtons_[static_cast<size_t>(i)].setToggleState(static_cast<int>(s.type) == i, juce::dontSendNotification);
    const bool studio = s.quality == sat::SatQuality::Studio;
    liveButton_.setToggleState(!studio, juce::dontSendNotification);
    studioButton_.setToggleState(studio, juce::dontSendNotification);
    qualityLabel_.setText(studio ? "4x oversampled, +28 samples latency" : "zero latency", juce::dontSendNotification);
    onButton_.setButtonText(s.enabled ? "SAT ON" : "SAT OFF");
}

void SatPanel::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(28);
    onButton_.setBounds(top.removeFromRight(100));
    top.removeFromRight(8);
    profileBox_.setBounds(top.removeFromLeft(juce::jmin(240, top.getWidth())));
    area.removeFromTop(6);

    auto types = area.removeFromTop(26);
    const int tw = types.getWidth() / sat::kNumSatTypes;
    for (auto& b : typeButtons_)
        b.setBounds(types.removeFromLeft(tw).reduced(2, 0));
    area.removeFromTop(8);

    auto bottom = area.removeFromBottom(112);
    area.removeFromBottom(10);
    const int side = juce::jmin(area.getHeight(), area.getWidth() / 2);
    curve_.setBounds(area.removeFromLeft(side));
    area.removeFromLeft(8);
    harmonics_.setBounds(area);

    const int kw = 86;
    for (auto* k : { &drive_, &mix_, &output_ })
    {
        auto cell = bottom.removeFromLeft(kw);
        k->label.setBounds(cell.removeFromTop(15));
        k->slider.setBounds(cell);
    }
    bottom.removeFromLeft(10);
    auto col = bottom.reduced(0, 8);
    autoGain_.setBounds(col.removeFromTop(26));
    col.removeFromTop(8);
    auto q = col.removeFromTop(26);
    liveButton_.setBounds(q.removeFromLeft(q.getWidth() / 2).reduced(1, 0));
    studioButton_.setBounds(q.reduced(1, 0));
    qualityLabel_.setBounds(col.removeFromTop(18));
}

// ----------------------------------------------------------- SatSuggestPanel

SatSuggestPanel::SatSuggestPanel(MixCoreProcessor& processor) : processor_(processor)
{
    listenButton_.setTooltip("Play the source normally for 8 seconds. MixCore measures its peaks and suggests a drive that adds a "
                             "typical amount of harmonics for this source. Nothing changes until you press Apply.");
    listenButton_.onClick = [this] {
        processor_.startSatListen(8.0);
        suggestions_.clear();
        rebuild();
    };
    addAndMakeVisible(listenButton_);
    addAndMakeVisible(progressBar_);
    for (auto* l : { &intro_, &stats_ })
    {
        l->setFont(theme::font(12.0f));
        l->setColour(juce::Label::textColourId, theme::label);
        l->setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(*l);
    }
    stats_.setFont(theme::font(11.5f, true));
    stats_.setColour(juce::Label::textColourId, theme::ice);
    rebuild();
}

void SatSuggestPanel::rebuild()
{
    for (auto& r : rows_)
    {
        removeChildComponent(r.title.get());
        removeChildComponent(r.why.get());
        removeChildComponent(r.apply.get());
    }
    rows_.clear();
    for (const auto& sug : suggestions_)
    {
        Row r;
        r.title = std::make_unique<juce::Label>();
        r.title->setText(juce::String(sug.title), juce::dontSendNotification);
        r.title->setFont(theme::font(12.5f, true));
        r.title->setColour(juce::Label::textColourId, theme::text);
        r.why = std::make_unique<juce::Label>();
        r.why->setText(juce::String(sug.why), juce::dontSendNotification);
        r.why->setFont(theme::font(10.5f));
        r.why->setColour(juce::Label::textColourId, theme::muted);
        r.why->setJustificationType(juce::Justification::topLeft);
        r.why->setMinimumHorizontalScale(1.0f);
        r.why->setTooltip(juce::String(sug.why));
        r.apply = std::make_unique<juce::TextButton>("Apply");
        r.apply->setTooltip("Set this and switch saturation on (undo works as usual).");
        auto* btn = r.apply.get();
        r.apply->onClick = [this, sug, btn] {
            processor_.applySatSuggestion(sug);
            btn->setButtonText("Applied");
            btn->setEnabled(false);
        };
        addAndMakeVisible(*r.title);
        addAndMakeVisible(*r.why);
        addAndMakeVisible(*r.apply);
        rows_.push_back(std::move(r));
    }
    resized();
}

void SatSuggestPanel::update()
{
    const bool listening = processor_.satListening();
    progress_ = static_cast<double>(processor_.satListenProgress());
    progressBar_.setVisible(listening);
    listenButton_.setEnabled(!listening);
    if (wasListening_ && !listening && processor_.satListenReady())
    {
        suggestions_ = processor_.satSuggestions();
        const auto st = processor_.satListenStats();
        stats_.setText("Loud peaks " + juce::String(juce::roundToInt(st.loudDb)) + "  |  typical " + juce::String(juce::roundToInt(st.typicalDb))
                           + " dBFS",
                       juce::dontSendNotification);
        rebuild();
    }
    wasListening_ = listening;

    const auto noun = juce::String(gain::words(processor_.currentChannelSettings().source).noun);
    juce::String text;
    if (listening)
        text = "Listening... play the " + noun + " normally. Pauses are ignored.";
    else if (!processor_.satListenReady())
        text = "Listen & suggest measures the peaks of the " + noun + " and proposes a drive that adds a typical amount of "
               "harmonics for it (subtle on voices, more on drums). Nothing changes until you press Apply.";
    intro_.setText(text, juce::dontSendNotification);
    intro_.setVisible(text.isNotEmpty());
    stats_.setVisible(!listening && processor_.satListenReady());
}

void SatSuggestPanel::paint(juce::Graphics& g)
{
    g.setColour(theme::panelEdge.withAlpha(0.5f));
    for (const auto& r : rows_)
        g.drawHorizontalLine(r.why->getBottom() + 2, 0.0f, static_cast<float>(getWidth()));
}

void SatSuggestPanel::resized()
{
    auto area = getLocalBounds();
    listenButton_.setBounds(area.removeFromTop(30));
    area.removeFromTop(6);
    progressBar_.setBounds(area.removeFromTop(8));
    area.removeFromTop(6);
    if (rows_.empty())
    {
        stats_.setBounds(area.removeFromTop(stats_.isVisible() ? 18 : 0));
        intro_.setBounds(area.removeFromTop(90));
        return;
    }
    intro_.setBounds({});
    stats_.setBounds(area.removeFromTop(18));
    area.removeFromTop(4);
    const int rowH = juce::jmin(118, area.getHeight() / static_cast<int>(rows_.size()));
    for (auto& r : rows_)
    {
        auto row = area.removeFromTop(rowH);
        r.apply->setBounds(row.removeFromRight(64).withSizeKeepingCentre(64, 24));
        row.removeFromRight(4);
        r.title->setBounds(row.removeFromTop(18));
        r.why->setBounds(row.withTrimmedBottom(4));
    }
}

} // namespace mixcore::plugin
