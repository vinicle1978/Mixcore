#include "CompPanel.h"

#include "mixcore/comp/CompProfiles.h"

#include <cmath>

namespace mixcore::plugin {

namespace {
juce::RangedAudioParameter* param(MixCoreProcessor& p, const char* id)
{
    return dynamic_cast<juce::RangedAudioParameter*>(p.state().getParameter(id));
}

void setParamValue(MixCoreProcessor& p, const char* id, float v)
{
    if (auto* prm = param(p, id))
        prm->setValueNotifyingHost(prm->convertTo0to1(v));
}

juce::Colour grColour(float gr) { return gr > 12.0f ? theme::warning : theme::blueLight; }
} // namespace

// ----------------------------------------------------------------- CompCurve

CompCurve::CompCurve(MixCoreProcessor& p) : processor_(p)
{
    setTooltip("Input level (across) against output level (up), before makeup. The dot is the level right now. Drag "
               "left/right to set the threshold; mouse wheel changes the ratio.");
}

void CompCurve::setLive(float detectorDb, float grDb, bool active)
{
    liveDb_ = detectorDb;
    liveGr_ = grDb;
    active_ = active;
    repaint();
}

juce::Rectangle<float> CompCurve::plot() const { return getLocalBounds().toFloat().reduced(8.0f).withTrimmedLeft(16.0f).withTrimmedBottom(12.0f); }
float CompCurve::xFor(float db) const
{
    const auto p = plot();
    return p.getX() + (juce::jlimit(kMinDb, 0.0f, db) - kMinDb) / -kMinDb * p.getWidth();
}
float CompCurve::yFor(float db) const
{
    const auto p = plot();
    return p.getBottom() - (juce::jlimit(kMinDb, 0.0f, db) - kMinDb) / -kMinDb * p.getHeight();
}
float CompCurve::dbForX(float x) const
{
    const auto p = plot();
    return kMinDb + juce::jlimit(0.0f, 1.0f, (x - p.getX()) / p.getWidth()) * -kMinDb;
}

void CompCurve::mouseDown(const juce::MouseEvent& e)
{
    dragging_ = true;
    if (auto* prm = param(processor_, ids::compThreshold))
        prm->beginChangeGesture();
    mouseDrag(e);
}

void CompCurve::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_)
        return;
    setParamValue(processor_, ids::compThreshold, std::round(dbForX(e.position.x) * 2.0f) * 0.5f);
    repaint();
}

void CompCurve::mouseUp(const juce::MouseEvent&)
{
    if (dragging_)
        if (auto* prm = param(processor_, ids::compThreshold))
            prm->endChangeGesture();
    dragging_ = false;
}

void CompCurve::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float r = processor_.currentChannelSettings().comp.ratio;
    if (auto* prm = param(processor_, ids::compRatio))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(juce::jlimit(1.0f, 20.0f, r * std::pow(1.12f, w.deltaY * 5.0f))));
        prm->endChangeGesture();
    }
    repaint();
}

void CompCurve::paint(juce::Graphics& g)
{
    theme::fillPanel(g, getLocalBounds().toFloat(), 6.0f, 0.15f);
    const auto p = plot();
    const auto s = processor_.currentChannelSettings().comp;

    g.setFont(theme::font(9.0f));
    for (int db = -60; db <= 0; db += 12)
    {
        g.setColour(theme::panelEdge.withAlpha(0.6f));
        g.drawVerticalLine(juce::roundToInt(xFor(static_cast<float>(db))), p.getY(), p.getBottom());
        g.drawHorizontalLine(juce::roundToInt(yFor(static_cast<float>(db))), p.getX(), p.getRight());
        g.setColour(theme::muted);
        g.drawText(juce::String(db), juce::Rectangle<float>(xFor(static_cast<float>(db)) - 12.0f, p.getBottom() + 1.0f, 24.0f, 11.0f),
                   juce::Justification::centred);
        if (db > -60)
            g.drawText(juce::String(db), juce::Rectangle<float>(0.0f, yFor(static_cast<float>(db)) - 6.0f, p.getX() - 2.0f, 12.0f),
                       juce::Justification::centredRight);
    }
    // 1:1 reference
    g.setColour(theme::silver.withAlpha(0.25f));
    g.drawLine(xFor(kMinDb), yFor(kMinDb), xFor(0.0f), yFor(0.0f), 1.0f);

    // Knee region and threshold
    if (s.kneeDb > 0.0f)
    {
        g.setColour(theme::blue.withAlpha(0.08f));
        const float x0 = xFor(s.thresholdDb - 0.5f * s.kneeDb), x1 = xFor(s.thresholdDb + 0.5f * s.kneeDb);
        g.fillRect(juce::Rectangle<float>(x0, p.getY(), x1 - x0, p.getHeight()));
    }
    {
        const float x = xFor(s.thresholdDb);
        const float dash[] = { 3.0f, 3.0f };
        g.setColour(theme::rider.withAlpha(0.7f));
        g.drawDashedLine(juce::Line<float>(x, p.getY(), x, p.getBottom()), dash, 2, 1.0f);
    }

    // Curve
    juce::Path curve;
    for (float in = kMinDb; in <= 0.001f; in += 0.25f)
    {
        const float out = in + comp::gainChangeDb(in, s.thresholdDb, s.ratio, s.kneeDb);
        const juce::Point<float> pt(xFor(in), yFor(out));
        if (in == kMinDb)
            curve.startNewSubPath(pt);
        else
            curve.lineTo(pt);
    }
    g.setColour(s.enabled ? theme::text : theme::muted.withAlpha(0.6f));
    g.strokePath(curve, juce::PathStrokeType(2.2f));

    // Live level
    if (liveDb_ > kMinDb)
    {
        const float out = liveDb_ + comp::gainChangeDb(liveDb_, s.thresholdDb, s.ratio, s.kneeDb);
        const auto c = juce::Point<float>(xFor(liveDb_), yFor(out));
        const auto col = active_ ? grColour(liveGr_) : theme::muted;
        g.setColour(col.withAlpha(0.35f));
        g.fillEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre(c));
        g.setColour(col);
        g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(c));
    }

    g.setColour(theme::muted);
    g.setFont(theme::font(10.0f, true));
    g.drawText(juce::String(s.ratio >= 19.95f ? "20:1 limit" : juce::String(s.ratio, 1) + ":1") + "  T " + juce::String(s.thresholdDb, 1),
               p.reduced(6.0f).removeFromBottom(14.0f), juce::Justification::bottomRight);
}

// --------------------------------------------------------------- CompHistory

CompHistory::CompHistory()
{
    setTooltip("Gain reduction over the last 10 seconds, and right now (bar on the right, with peak hold). Orange above 12 dB.");
}

void CompHistory::push(float grDb)
{
    current_ = grDb;
    gr_[static_cast<size_t>(write_)] = grDb;
    write_ = (write_ + 1) % static_cast<int>(gr_.size());
    if (grDb >= hold_)
    {
        hold_ = grDb;
        holdAge_ = 0;
    }
    else if (++holdAge_ > 60)
        hold_ = juce::jmax(grDb, hold_ - 0.5f);
    repaint();
}

void CompHistory::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    theme::fillPanel(g, area, 6.0f, 0.15f);
    area.reduce(6.0f, 6.0f);
    auto meter = area.removeFromRight(14.0f);
    area.removeFromRight(6.0f);
    area.removeFromLeft(18.0f);
    constexpr float kRange = 24.0f;
    const auto yFor = [&](float db) { return area.getY() + juce::jlimit(0.0f, 1.0f, db / kRange) * area.getHeight(); };

    g.setFont(theme::font(9.0f));
    for (int db : { 0, 6, 12, 18, 24 })
    {
        g.setColour(theme::panelEdge.withAlpha(0.6f));
        g.drawHorizontalLine(juce::roundToInt(yFor(static_cast<float>(db))), area.getX(), area.getRight());
        g.setColour(theme::muted);
        g.drawText(db == 0 ? "0" : "-" + juce::String(db), juce::Rectangle<float>(area.getX() - 20.0f, yFor(static_cast<float>(db)) - 6.0f, 17.0f, 12.0f),
                   juce::Justification::centredRight);
    }

    const int n = static_cast<int>(gr_.size());
    const float w = area.getWidth() / static_cast<float>(n);
    for (int i = 0; i < n; ++i)
    {
        const float v = gr_[static_cast<size_t>((write_ + i) % n)];
        if (v < 0.05f)
            continue;
        g.setColour(grColour(v).withAlpha(0.8f));
        g.fillRect(juce::Rectangle<float>(area.getX() + static_cast<float>(i) * w, area.getY(), w + 0.5f, yFor(v) - area.getY()));
    }

    // Meter
    g.setColour(theme::background);
    g.fillRoundedRectangle(meter, 3.0f);
    if (current_ > 0.05f)
    {
        const auto bar = meter.withHeight(yFor(current_) - meter.getY());
        g.setGradientFill(current_ > 12.0f ? theme::orangeGradient(bar) : theme::blueGradient(bar));
        g.fillRoundedRectangle(bar, 3.0f);
    }
    g.setColour(theme::text);
    g.fillRect(juce::Rectangle<float>(meter.getX(), yFor(hold_) - 1.0f, meter.getWidth(), 2.0f));
}

// ----------------------------------------------------------------- CompPanel

CompPanel::CompPanel(MixCoreProcessor& processor) : processor_(processor), curve_(processor)
{
    addAndMakeVisible(curve_);
    addAndMakeVisible(history_);
    grLabel_.setFont(theme::font(13.0f, true));
    grLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(grLabel_);

    profileBox_.addSectionHeading("Compressor profiles (starting points)");
    const auto& profiles = comp::compProfiles();
    for (size_t i = 0; i < profiles.size(); ++i)
        profileBox_.addItem(juce::String(profiles[i].name), static_cast<int>(i) + 1);
    profileBox_.setText("Load comp profile...", juce::dontSendNotification);
    profileBox_.setTooltip("Load starting settings for this source (switches the compressor on). Then fine-tune, or use Listen & suggest.");
    profileBox_.onChange = [this] {
        const int id = profileBox_.getSelectedId();
        const auto& list = comp::compProfiles();
        if (id > 0 && id <= static_cast<int>(list.size()))
            processor_.loadCompProfile(juce::String(list[static_cast<size_t>(id - 1)].name));
        profileBox_.setSelectedId(0, juce::dontSendNotification);
        profileBox_.setText("Load comp profile...", juce::dontSendNotification);
    };
    addAndMakeVisible(profileBox_);

    onButton_.setClickingTogglesState(true);
    onButton_.setTooltip("Switch the compressor on or off (10 ms crossfade). Off = not processing. Zero latency.");
    onAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor_.state(), ids::compOn, onButton_);
    addAndMakeVisible(onButton_);

    setupKnob(threshold_, ids::compThreshold, "Threshold", theme::rider, "Level above which the compressor starts reducing.");
    setupKnob(ratio_, ids::compRatio, "Ratio", theme::level, "How strongly levels above the threshold are reduced. 2:1 gentle, 4:1 firm, 20:1 limiting.");
    setupKnob(attack_, ids::compAttack, "Attack", theme::silver,
              "How fast it reacts. Slower attack lets the start of each note or hit through (punch); faster catches peaks.");
    setupKnob(release_, ids::compRelease, "Release", theme::silver,
              "How fast it lets go. With Auto release this is the fast part; long, steady passages release more slowly.");
    setupKnob(makeup_, ids::compMakeup, "Makeup", theme::level, "Gain after compression, to bring the level back up.");
    setupKnob(mix_, ids::compMix, "Mix", theme::blueLight,
              "Parallel compression: 100 % = fully compressed, lower values blend the dry signal back in (no delay, no phasing).");
    setupKnob(knee_, ids::compKnee, "Knee", theme::silver, "Soft knee: compression starts gradually around the threshold. 0 = hard knee.");
    setupKnob(hpf_, ids::compScHpf, "SC HPF", theme::silver,
              "Sidechain high-pass: low end below this frequency doesn't drive the compressor (no pumping from kick or plosives).");

    autoRelease_.setTooltip("Program-dependent release: fast after short peaks, slower after long loud passages (less pumping).");
    autoMakeup_.setTooltip("Makeup follows the average reduction (slowly), so switching the compressor on keeps the level about the same.");
    hpfOn_.setTooltip("Filter the low end out of the signal the compressor listens to (not out of the audio).");
    auto& st = processor_.state();
    autoReleaseAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(st, ids::compAutoRelease, autoRelease_);
    autoMakeupAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(st, ids::compAutoMakeup, autoMakeup_);
    hpfOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(st, ids::compScHpfOn, hpfOn_);
    for (auto* b : { &autoRelease_, &autoMakeup_, &hpfOn_ })
        addAndMakeVisible(*b);
    detectorBox_.addItemList({ "Peak", "RMS" }, 1);
    detectorBox_.setTooltip("Peak reacts to every transient (drums). RMS follows loudness (voices, instruments).");
    detectorAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(st, ids::compDetector, detectorBox_);
    addAndMakeVisible(detectorBox_);
    detectorLabel_.setText("Detector", juce::dontSendNotification);
    detectorLabel_.setFont(theme::font(11.5f, true));
    detectorLabel_.setColour(juce::Label::textColourId, theme::label);
    addAndMakeVisible(detectorLabel_);
}

void CompPanel::setupKnob(Knob& k, const char* paramId, const juce::String& caption, juce::Colour accent, const juce::String& tip)
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

void CompPanel::update(const comp::CompTelemetry& t, bool fresh)
{
    if (fresh)
    {
        history_.push(t.gainReductionDb);
        curve_.setLive(t.detectorDb, t.gainReductionDb, t.enabled);
    }
    const auto s = processor_.currentChannelSettings().comp;
    onButton_.setButtonText(s.enabled ? "COMP ON" : "COMP OFF");
    juce::String text = s.enabled ? "GR " + juce::String(t.gainReductionDb, 1) + " dB" : "off";
    if (s.enabled && s.autoMakeup)
        text << "   makeup +" << juce::String(t.makeupDb, 1);
    grLabel_.setText(text, juce::dontSendNotification);
    grLabel_.setColour(juce::Label::textColourId, s.enabled ? grColour(t.gainReductionDb) : theme::muted);
    makeup_.slider.setEnabled(!s.autoMakeup);
    makeup_.slider.setAlpha(s.autoMakeup ? 0.45f : 1.0f);
    hpf_.slider.setAlpha(s.scHpfOn ? 1.0f : 0.45f);
}

void CompPanel::paint(juce::Graphics& g)
{
    g.setColour(theme::panelEdge.withAlpha(0.5f));
    g.drawHorizontalLine(threshold_.label.getY() - 6, 0.0f, static_cast<float>(getWidth()));
}

void CompPanel::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(28);
    onButton_.setBounds(top.removeFromRight(100));
    top.removeFromRight(8);
    profileBox_.setBounds(top.removeFromLeft(juce::jmin(240, top.getWidth())));
    area.removeFromTop(6);

    auto row2 = area.removeFromBottom(98);
    area.removeFromBottom(4);
    auto row1 = area.removeFromBottom(100);
    area.removeFromBottom(12);

    // Graphs: square curve on the left, history with GR readout on the right.
    const int side = juce::jmin(area.getHeight(), area.getWidth() / 2);
    curve_.setBounds(area.removeFromLeft(side));
    area.removeFromLeft(8);
    grLabel_.setBounds(area.removeFromTop(18));
    history_.setBounds(area);

    const int kw = row1.getWidth() / 6;
    for (auto* k : { &threshold_, &ratio_, &attack_, &release_, &makeup_, &mix_ })
    {
        auto cell = row1.removeFromLeft(kw);
        k->label.setBounds(cell.removeFromTop(15));
        k->slider.setBounds(cell);
    }
    for (auto* k : { &knee_, &hpf_ })
    {
        auto cell = row2.removeFromLeft(kw);
        k->label.setBounds(cell.removeFromTop(15));
        k->slider.setBounds(cell);
    }
    row2.removeFromLeft(10);
    auto toggles = row2.removeFromLeft(juce::jmin(130, row2.getWidth() / 2));
    const int th = toggles.getHeight() / 3;
    autoRelease_.setBounds(toggles.removeFromTop(th));
    autoMakeup_.setBounds(toggles.removeFromTop(th));
    hpfOn_.setBounds(toggles);
    row2.removeFromLeft(8);
    auto det = row2.withSizeKeepingCentre(row2.getWidth(), 46);
    detectorLabel_.setBounds(det.removeFromTop(18));
    detectorBox_.setBounds(det.removeFromTop(26).withWidth(juce::jmin(110, det.getWidth())));
}

// ---------------------------------------------------------- CompSuggestPanel

CompSuggestPanel::CompSuggestPanel(MixCoreProcessor& processor) : processor_(processor)
{
    listenButton_.setTooltip("Play the source normally for 8 seconds. MixCore measures its loud and quiet levels and suggests "
                             "compressor settings. Nothing changes until you press Apply.");
    listenButton_.onClick = [this] {
        processor_.startCompListen(8.0);
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

void CompSuggestPanel::rebuild()
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
        r.apply->setTooltip("Set these values and switch the compressor on (undo works as usual).");
        auto* btn = r.apply.get();
        r.apply->onClick = [this, sug, btn] {
            processor_.applyCompSuggestion(sug);
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

void CompSuggestPanel::update()
{
    const bool listening = processor_.compListening();
    progress_ = static_cast<double>(processor_.compListenProgress());
    progressBar_.setVisible(listening);
    listenButton_.setEnabled(!listening);
    if (wasListening_ && !listening && processor_.compListenReady())
    {
        suggestions_ = processor_.compSuggestions();
        const auto st = processor_.compListenStats();
        stats_.setText("Loud " + juce::String(juce::roundToInt(st.loudDb)) + "  |  typical " + juce::String(juce::roundToInt(st.typicalDb))
                           + "  |  quiet " + juce::String(juce::roundToInt(st.quietDb)) + " dBFS",
                       juce::dontSendNotification);
        rebuild();
    }
    wasListening_ = listening;

    const auto noun = juce::String(gain::words(processor_.currentChannelSettings().source).noun);
    juce::String text;
    if (listening)
        text = "Listening... play the " + noun + " normally, loud and quiet parts. Pauses are ignored.";
    else if (!processor_.compListenReady())
        text = "Listen & suggest measures how loud the loud and quiet parts of the " + noun + " are and proposes a threshold, "
               "ratio and makeup for a typical amount of compression. Nothing changes until you press Apply.";
    intro_.setText(text, juce::dontSendNotification);
    intro_.setVisible(text.isNotEmpty());
    stats_.setVisible(!listening && processor_.compListenReady());
}

void CompSuggestPanel::paint(juce::Graphics& g)
{
    g.setColour(theme::panelEdge.withAlpha(0.5f));
    for (const auto& r : rows_)
        g.drawHorizontalLine(r.why->getBottom() + 2, 0.0f, static_cast<float>(getWidth()));
}

void CompSuggestPanel::resized()
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
