#include "EqPanel.h"

#include "mixcore/eq/EqFilters.h"
#include "mixcore/eq/EqProfiles.h"

#include <cmath>

namespace mixcore::plugin {

namespace {
constexpr double kMinF = 20.0, kMaxF = 20000.0;
constexpr float kAnaTop = -10.0f, kAnaBottom = -100.0f; // analyzer scale (dBFS)
const char* kBandShort[eq::kNumBands] = { "HP", "LS", "1", "2", "3", "HS", "LP" };

juce::RangedAudioParameter* param(MixCoreProcessor& p, const juce::String& id)
{
    return dynamic_cast<juce::RangedAudioParameter*>(p.state().getParameter(id));
}
} // namespace

// ------------------------------------------------------------------- EqCurve

EqCurve::EqCurve(MixCoreProcessor& p) : processor_(p)
{
    setTooltip("Drag a point to move a band (left/right = frequency, up/down = gain). Mouse wheel over a point = Q "
               "(width). Double-click a point = band on/off. Shaded: live spectrum after the EQ; faint line: before.");
}

const std::vector<double>& EqCurve::displayFreqs()
{
    static const std::vector<double> f = [] {
        std::vector<double> v;
        for (int i = 0; i < 200; ++i)
            v.push_back(kMinF * std::pow(kMaxF / kMinF, i / 199.0));
        return v;
    }();
    return f;
}

void EqCurve::setSpectrum(const std::vector<float>& inDb, const std::vector<float>& outDb)
{
    inDb_ = inDb;
    outDb_ = outDb;
    repaint();
}

void EqCurve::setSelected(int band)
{
    selected_ = band;
    repaint();
}

juce::Rectangle<float> EqCurve::plot() const { return getLocalBounds().toFloat().reduced(10.0f, 8.0f).withTrimmedLeft(22.0f).withTrimmedBottom(12.0f); }

float EqCurve::xForFreq(double f) const
{
    const auto p = plot();
    return p.getX() + static_cast<float>(std::log(f / kMinF) / std::log(kMaxF / kMinF)) * p.getWidth();
}

double EqCurve::freqForX(float x) const
{
    const auto p = plot();
    const double t = juce::jlimit(0.0, 1.0, static_cast<double>((x - p.getX()) / p.getWidth()));
    return kMinF * std::pow(kMaxF / kMinF, t);
}

float EqCurve::yForDb(double db) const
{
    const auto p = plot();
    const double t = juce::jlimit(-1.0, 1.0, db / kRangeDb);
    return p.getCentreY() - static_cast<float>(t) * p.getHeight() * 0.5f;
}

double EqCurve::dbForY(float y) const
{
    const auto p = plot();
    return juce::jlimit(-kRangeDb, kRangeDb, static_cast<double>((p.getCentreY() - y) / (p.getHeight() * 0.5f)) * kRangeDb);
}

juce::Point<float> EqCurve::nodePos(const eq::EqSettings& s, int band) const
{
    const auto& b = s.bands[static_cast<size_t>(band)];
    const bool hasGain = eqBandHasGain(band);
    return { xForFreq(static_cast<double>(b.freqHz)), yForDb(hasGain ? static_cast<double>(b.gainDb) : 0.0) };
}

int EqCurve::nodeAt(juce::Point<float> p) const
{
    const auto s = processor_.currentChannelSettings().eq;
    int best = -1;
    float bestD = 14.0f;
    for (int b = 0; b < eq::kNumBands; ++b)
    {
        const float d = nodePos(s, b).getDistanceFrom(p);
        if (d < bestD)
            best = b, bestD = d;
    }
    return best;
}

void EqCurve::setParam(const juce::String& id, float value, bool gesture)
{
    if (auto* prm = param(processor_, id))
    {
        if (gesture)
            prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(value));
        if (gesture)
            prm->endChangeGesture();
    }
}

void EqCurve::mouseDown(const juce::MouseEvent& e)
{
    dragging_ = nodeAt(e.position);
    if (dragging_ < 0)
        return;
    selected_ = dragging_;
    if (onSelect)
        onSelect(dragging_);
    for (auto f : { EqField::Freq, EqField::Gain })
        if (f == EqField::Freq || eqBandHasGain(dragging_))
            if (auto* prm = param(processor_, eqParamId(dragging_, f)))
                prm->beginChangeGesture();
}

void EqCurve::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging_ < 0)
        return;
    setParam(eqParamId(dragging_, EqField::Freq), static_cast<float>(freqForX(e.position.x)), false);
    if (eqBandHasGain(dragging_))
        setParam(eqParamId(dragging_, EqField::Gain), static_cast<float>(std::round(dbForY(e.position.y) * 10.0) / 10.0), false);
    repaint();
}

void EqCurve::mouseUp(const juce::MouseEvent&)
{
    if (dragging_ < 0)
        return;
    for (auto f : { EqField::Freq, EqField::Gain })
        if (f == EqField::Freq || eqBandHasGain(dragging_))
            if (auto* prm = param(processor_, eqParamId(dragging_, f)))
                prm->endChangeGesture();
    dragging_ = -1;
}

void EqCurve::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int b = nodeAt(e.position);
    if (b < 0)
        return;
    const bool on = processor_.currentChannelSettings().eq.bands[static_cast<size_t>(b)].on;
    setParam(eqParamId(b, EqField::On), on ? 0.0f : 1.0f, true);
    repaint();
}

void EqCurve::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int b = nodeAt(e.position);
    if (b < 0)
        b = selected_;
    const float q = processor_.currentChannelSettings().eq.bands[static_cast<size_t>(b)].q;
    setParam(eqParamId(b, EqField::Q), juce::jlimit(0.1f, 10.0f, q * std::pow(1.15f, w.deltaY * 5.0f)), true);
    repaint();
}

void EqCurve::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    theme::fillPanel(g, area, 6.0f, 0.15f);
    const auto p = plot();
    const auto s = processor_.currentChannelSettings().eq;
    const double sr = processor_.eqSampleRate();

    // Grid
    g.setFont(theme::font(9.5f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xForFreq(f);
        g.setColour(theme::panelEdge.withAlpha(0.6f));
        g.drawVerticalLine(static_cast<int>(x), p.getY(), p.getBottom());
        g.setColour(theme::muted);
        g.drawText(f >= 1000.0 ? juce::String(static_cast<int>(f / 1000.0)) + "k" : juce::String(static_cast<int>(f)),
                   juce::Rectangle<float>(x - 15.0f, p.getBottom() + 1.0f, 30.0f, 11.0f), juce::Justification::centred);
    }
    for (int db : { -12, -6, 0, 6, 12 })
    {
        const float y = yForDb(db);
        g.setColour(theme::panelEdge.withAlpha(db == 0 ? 1.0f : 0.6f));
        g.drawHorizontalLine(static_cast<int>(y), p.getX(), p.getRight());
        g.setColour(theme::muted);
        g.drawText((db > 0 ? "+" : "") + juce::String(db), juce::Rectangle<float>(area.getX() + 2.0f, y - 6.0f, 26.0f, 12.0f),
                   juce::Justification::centredLeft);
    }

    // Analyzer (its own scale: -100 .. -10 dBFS over the plot height)
    const auto& freqs = displayFreqs();
    const auto anaY = [&](float db) {
        const float t = juce::jlimit(0.0f, 1.0f, (db - kAnaBottom) / (kAnaTop - kAnaBottom));
        return p.getBottom() - t * p.getHeight();
    };
    if (outDb_.size() == freqs.size())
    {
        juce::Path fill;
        fill.startNewSubPath(p.getX(), p.getBottom());
        for (size_t i = 0; i < freqs.size(); ++i)
            fill.lineTo(xForFreq(freqs[i]), anaY(outDb_[i]));
        fill.lineTo(p.getRight(), p.getBottom());
        fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(theme::blue.withAlpha(0.35f), p.getCentreX(), p.getY(), theme::blueDeep.withAlpha(0.1f),
                                               p.getCentreX(), p.getBottom(), false));
        g.fillPath(fill);
    }
    if (inDb_.size() == freqs.size())
    {
        juce::Path line;
        for (size_t i = 0; i < freqs.size(); ++i)
        {
            const juce::Point<float> pt(xForFreq(freqs[i]), anaY(inDb_[i]));
            if (i == 0)
                line.startNewSubPath(pt);
            else
                line.lineTo(pt);
        }
        g.setColour(theme::silver.withAlpha(0.35f));
        g.strokePath(line, juce::PathStrokeType(1.0f));
    }

    // Selected band's own curve
    {
        juce::Path band;
        band.startNewSubPath(p.getX(), yForDb(0.0));
        for (double f : freqs)
            band.lineTo(xForFreq(f), yForDb(eq::bandResponseDb(s, selected_, sr, f)));
        band.lineTo(p.getRight(), yForDb(0.0));
        band.closeSubPath();
        g.setColour(theme::blueLight.withAlpha(s.bands[static_cast<size_t>(selected_)].on ? 0.18f : 0.06f));
        g.fillPath(band);
    }

    // Total response (drawn even when the EQ is off, dimmed)
    auto shown = s;
    shown.enabled = true;
    juce::Path curve;
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        const juce::Point<float> pt(xForFreq(freqs[i]), yForDb(eq::responseDb(shown, sr, freqs[i])));
        if (i == 0)
            curve.startNewSubPath(pt);
        else
            curve.lineTo(pt);
    }
    g.setColour(s.enabled ? theme::text : theme::muted.withAlpha(0.6f));
    g.strokePath(curve, juce::PathStrokeType(2.2f));

    // Nodes
    g.setFont(theme::font(9.5f, true));
    for (int b = 0; b < eq::kNumBands; ++b)
    {
        const auto pos = nodePos(s, b);
        const bool on = s.bands[static_cast<size_t>(b)].on;
        const auto r = juce::Rectangle<float>(16.0f, 16.0f).withCentre(pos);
        if (on)
        {
            g.setGradientFill(theme::blueGradient(r));
            g.fillEllipse(r);
        }
        else
        {
            g.setColour(theme::background.withAlpha(0.8f));
            g.fillEllipse(r);
        }
        g.setColour(b == selected_ ? theme::text : (on ? theme::blueLight : theme::muted));
        g.drawEllipse(r, b == selected_ ? 2.0f : 1.2f);
        g.setColour(on ? theme::text : theme::muted);
        g.drawText(kBandShort[b], r, juce::Justification::centred);
    }
    if (!s.enabled)
    {
        g.setColour(theme::muted);
        g.setFont(theme::font(10.0f, true));
        g.drawText("EQ OFF - curve shown for setting up", p.withHeight(14.0f).withTrimmedRight(4.0f), juce::Justification::topRight);
    }
}

// ------------------------------------------------------------------- EqPanel

EqPanel::EqPanel(MixCoreProcessor& processor) : processor_(processor), curve_(processor)
{
    addAndMakeVisible(curve_);
    curve_.onSelect = [this](int b) { selectBand(b); };

    profileBox_.addSectionHeading("EQ profiles (starting points)");
    const auto& profiles = eq::eqProfiles();
    for (size_t i = 0; i < profiles.size(); ++i)
        profileBox_.addItem(juce::String(profiles[i].name), static_cast<int>(i) + 1);
    profileBox_.setText("Load EQ profile...", juce::dontSendNotification);
    profileBox_.setTooltip("Load a starting EQ for this source (switches the EQ on). Then use your ears, or Listen & suggest.");
    profileBox_.onChange = [this] {
        const int id = profileBox_.getSelectedId();
        const auto& list = eq::eqProfiles();
        if (id > 0 && id <= static_cast<int>(list.size()))
            processor_.loadEqProfile(juce::String(list[static_cast<size_t>(id - 1)].name));
        profileBox_.setSelectedId(0, juce::dontSendNotification);
        profileBox_.setText("Load EQ profile...", juce::dontSendNotification);
    };
    addAndMakeVisible(profileBox_);

    onButton_.setClickingTogglesState(true);
    onButton_.setTooltip("Switch the EQ on or off (10 ms crossfade). Off = not processing.");
    onAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor_.state(), ids::eqOn, onButton_);
    addAndMakeVisible(onButton_);

    for (int b = 0; b < eq::kNumBands; ++b)
    {
        auto& btn = bandButtons_[static_cast<size_t>(b)];
        btn.setButtonText(kBandShort[b]);
        btn.setClickingTogglesState(true);
        btn.setRadioGroupId(5511);
        btn.setTooltip(juce::String(eq::EqSettings::bandName(b)) + ": click to edit.");
        btn.onClick = [this, b] {
            if (bandButtons_[static_cast<size_t>(b)].getToggleState())
                selectBand(b);
        };
        addAndMakeVisible(btn);
    }

    bandTitle_.setFont(theme::font(13.0f, true));
    bandTitle_.setColour(juce::Label::textColourId, theme::text);
    addAndMakeVisible(bandTitle_);
    bandOn_.setTooltip("Switch this band on or off (10 ms crossfade).");
    addAndMakeVisible(bandOn_);
    setupKnob(freq_, "Freq", theme::level);
    setupKnob(gain_, "Gain", theme::blueLight);
    setupKnob(q_, "Q (width)", theme::silver);
    setupKnob(output_, "Output", theme::silver);
    freq_.slider.setTooltip("Band frequency.");
    gain_.slider.setTooltip("Boost or cut at this frequency.");
    q_.slider.setTooltip("Width: low Q = broad, high Q = narrow. For 12 dB filters, Q > 0.7 adds a resonant bump.");
    output_.slider.setTooltip("Overall EQ output level (to level-match after big boosts or cuts).");
    output_.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor_.state(), ids::eqOutput, output_.slider);
    slopeBox_.addItemList({ "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1);
    slopeBox_.setTooltip("Steepness of the filter.");
    addAndMakeVisible(slopeBox_);
    selectBand(2);
}

void EqPanel::setupKnob(Knob& k, const juce::String& caption, juce::Colour accent)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 16);
    k.slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.label.setText(caption, juce::dontSendNotification);
    k.label.setFont(theme::font(12.0f, true));
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, theme::label);
    addAndMakeVisible(k.slider);
    addAndMakeVisible(k.label);
}

void EqPanel::selectBand(int band)
{
    selected_ = juce::jlimit(0, eq::kNumBands - 1, band);
    bandButtons_[static_cast<size_t>(selected_)].setToggleState(true, juce::dontSendNotification);
    curve_.setSelected(selected_);
    bindBand();
    resized();
}

void EqPanel::bindBand()
{
    auto& st = processor_.state();
    const int b = selected_;
    // Detach before re-attaching (attachments hold the slider's value).
    bandOnAttachment_.reset();
    freq_.attachment.reset();
    gain_.attachment.reset();
    q_.attachment.reset();
    slopeAttachment_.reset();
    bandOnAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(st, eqParamId(b, EqField::On), bandOn_);
    freq_.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(st, eqParamId(b, EqField::Freq), freq_.slider);
    q_.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(st, eqParamId(b, EqField::Q), q_.slider);
    const bool hasGain = eqBandHasGain(b);
    gain_.slider.setVisible(hasGain);
    gain_.label.setVisible(hasGain);
    slopeBox_.setVisible(!hasGain);
    if (hasGain)
        gain_.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(st, eqParamId(b, EqField::Gain), gain_.slider);
    else
        slopeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(st, eqParamId(b, EqField::Slope), slopeBox_);
    bandTitle_.setText(eq::EqSettings::bandName(b), juce::dontSendNotification);
}

void EqPanel::update()
{
    // Analyzer: newest 4096 samples before/after the EQ, 1/6-octave smoothed, averaged over time.
    const auto& in = processor_.eqInputTap();
    const auto& out = processor_.eqOutputTap();
    const auto written = out.written();
    if (written != lastWritten_ && written >= 4096)
    {
        lastWritten_ = written;
        frame_.resize(4096);
        const double sr = processor_.eqSampleRate();
        const auto spectrum = [&](const eq::SpectrumTap& tap, std::vector<float>& avg) {
            tap.readLatest(frame_.data(), 4096);
            const auto p = eq::powerSpectrum(frame_.data(), fft_);
            const auto db = eq::smoothedSpectrumDb(p, sr, EqCurve::displayFreqs());
            if (avg.size() != db.size())
                avg = db;
            for (size_t i = 0; i < db.size(); ++i)
                avg[i] = db[i] > avg[i] ? db[i] : 0.8f * avg[i] + 0.2f * db[i]; // fast up, slow down
        };
        spectrum(in, avgIn_);
        spectrum(out, avgOut_);
        curve_.setSpectrum(avgIn_, avgOut_);
    }
    else
    {
        curve_.repaint();
    }
    onButton_.setButtonText(onButton_.getToggleState() ? "EQ ON" : "EQ OFF");
    const auto s = processor_.currentChannelSettings().eq;
    for (int b = 0; b < eq::kNumBands; ++b)
        bandButtons_[static_cast<size_t>(b)].setColour(juce::TextButton::textColourOffId,
                                                        s.bands[static_cast<size_t>(b)].on ? theme::text : theme::muted.withAlpha(0.6f));
}

void EqPanel::paint(juce::Graphics& g)
{
    // Frame around the selected band's controls.
    const auto r = freq_.label.getBounds().getUnion(q_.slider.getBounds()).toFloat().expanded(6.0f, 4.0f);
    g.setColour(theme::panelEdge.withAlpha(0.6f));
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
}

void EqPanel::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(28);
    onButton_.setBounds(top.removeFromRight(100));
    top.removeFromRight(8);
    profileBox_.setBounds(top.removeFromLeft(juce::jmin(240, top.getWidth())));
    area.removeFromTop(6);

    auto controls = area.removeFromBottom(122);
    auto selector = area.removeFromBottom(26);
    area.removeFromBottom(6);
    curve_.setBounds(area);

    const int bw = selector.getWidth() / eq::kNumBands;
    for (int b = 0; b < eq::kNumBands; ++b)
        bandButtons_[static_cast<size_t>(b)].setBounds(selector.removeFromLeft(bw).reduced(2, 0));

    controls.removeFromTop(8);
    auto outCell = controls.removeFromRight(86);
    output_.label.setBounds(outCell.removeFromTop(16));
    output_.slider.setBounds(outCell);
    controls.removeFromRight(10);
    auto left = controls.removeFromLeft(118);
    bandTitle_.setBounds(left.removeFromTop(22));
    bandOn_.setBounds(left.removeFromTop(26));
    left.removeFromTop(6);
    slopeBox_.setBounds(left.removeFromTop(24));
    const int kw = controls.getWidth() / 3;
    for (auto* k : { &freq_, &gain_, &q_ })
    {
        auto cell = controls.removeFromLeft(kw);
        k->label.setBounds(cell.removeFromTop(16));
        k->slider.setBounds(cell);
    }
    repaint();
}

// ------------------------------------------------------------ EqSuggestPanel

EqSuggestPanel::EqSuggestPanel(MixCoreProcessor& processor) : processor_(processor)
{
    listenButton_.setTooltip("Play the source normally for 8 seconds. MixCore analyses its spectrum and suggests EQ moves. "
                             "Nothing changes until you press Apply.");
    listenButton_.onClick = [this] {
        processor_.startEqListen(8.0);
        suggestions_.clear();
        rebuild();
    };
    applyAllButton_.onClick = [this] {
        for (const auto& s : suggestions_)
            processor_.applyEqSuggestion(s);
        for (auto& r : rows_)
        {
            r.apply->setButtonText("Applied");
            r.apply->setEnabled(false);
        }
        applyAllButton_.setEnabled(false);
    };
    addAndMakeVisible(listenButton_);
    addAndMakeVisible(applyAllButton_);
    addAndMakeVisible(progressBar_);
    intro_.setFont(theme::font(12.5f));
    intro_.setColour(juce::Label::textColourId, theme::label);
    intro_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(intro_);
    rebuild();
}

void EqSuggestPanel::rebuild()
{
    for (auto& r : rows_)
    {
        removeChildComponent(r.title.get());
        removeChildComponent(r.why.get());
        removeChildComponent(r.apply.get());
    }
    rows_.clear();
    for (size_t i = 0; i < suggestions_.size(); ++i)
    {
        Row r;
        r.title = std::make_unique<juce::Label>();
        r.title->setText(juce::String(suggestions_[i].title), juce::dontSendNotification);
        r.title->setFont(theme::font(12.5f, true));
        r.title->setColour(juce::Label::textColourId, theme::text);
        r.why = std::make_unique<juce::Label>();
        r.why->setText(juce::String(suggestions_[i].why), juce::dontSendNotification);
        r.why->setFont(theme::font(10.5f));
        r.why->setColour(juce::Label::textColourId, theme::muted);
        r.why->setJustificationType(juce::Justification::topLeft);
        r.why->setMinimumHorizontalScale(1.0f);
        r.why->setTooltip(juce::String(suggestions_[i].why));
        r.apply = std::make_unique<juce::TextButton>("Apply");
        r.apply->setTooltip("Set " + juce::String(eq::EqSettings::bandName(suggestions_[i].band)) + " to this and switch the EQ on "
                            "(undo works as usual).");
        const auto s = suggestions_[i];
        auto* btn = r.apply.get();
        r.apply->onClick = [this, s, btn] {
            processor_.applyEqSuggestion(s);
            btn->setButtonText("Applied");
            btn->setEnabled(false);
        };
        addAndMakeVisible(*r.title);
        addAndMakeVisible(*r.why);
        addAndMakeVisible(*r.apply);
        rows_.push_back(std::move(r));
    }
    applyAllButton_.setVisible(suggestions_.size() > 1);
    applyAllButton_.setEnabled(true);
    resized();
}

void EqSuggestPanel::update()
{
    const bool listening = processor_.eqListening();
    progress_ = static_cast<double>(processor_.eqListenProgress());
    progressBar_.setVisible(listening);
    listenButton_.setEnabled(!listening);
    if (wasListening_ && !listening && processor_.eqListenReady())
    {
        suggestions_ = processor_.eqSuggestions();
        rebuild();
    }
    wasListening_ = listening;

    const auto noun = juce::String(gain::words(processor_.currentChannelSettings().source).noun);
    juce::String text;
    if (listening)
        text = "Listening... play the " + noun + " normally. Quiet moments are ignored.";
    else if (processor_.eqListenReady() && suggestions_.empty())
        text = "No changes suggested: the spectrum looks balanced for a " + noun + ". Trust your ears.";
    else if (!processor_.eqListenReady())
        text = "Listen & suggest analyses the " + noun + " going into the EQ and proposes a few EQ moves (high-pass, "
               "resonances, mud, harshness, air), each with a reason. Nothing changes until you press Apply.";
    intro_.setText(text, juce::dontSendNotification);
    intro_.setVisible(text.isNotEmpty());
}

void EqSuggestPanel::paint(juce::Graphics& g)
{
    g.setColour(theme::panelEdge.withAlpha(0.5f));
    for (const auto& r : rows_)
        g.drawHorizontalLine(r.why->getBottom() + 2, 0.0f, static_cast<float>(getWidth()));
}

void EqSuggestPanel::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(30);
    applyAllButton_.setBounds(top.removeFromRight(applyAllButton_.isVisible() ? 90 : 0));
    if (applyAllButton_.isVisible())
        top.removeFromRight(6);
    listenButton_.setBounds(top);
    area.removeFromTop(6);
    progressBar_.setBounds(area.removeFromTop(8));
    area.removeFromTop(6);
    if (rows_.empty())
    {
        intro_.setBounds(area.removeFromTop(90));
        return;
    }
    intro_.setBounds({});
    const int rowH = juce::jmin(86, area.getHeight() / static_cast<int>(rows_.size()));
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
