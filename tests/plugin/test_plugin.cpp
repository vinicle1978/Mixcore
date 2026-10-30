// Plugin adapter tests: parameters map onto the engine, audio flows through
// the real juce::AudioProcessor, state restores, host bypass and latency are
// correct, and the editor can be constructed and rendered.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "mixcore/drum/DrumProfiles.h"
#include "mixcore/gain/SmartGainPresets.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>

using namespace mixcore;
using namespace mixcore::plugin;
using Catch::Approx;

namespace {

struct JuceEnv
{
    juce::ScopedJuceInitialiser_GUI init;
};

void setParam(MixCoreProcessor& p, const char* id, float value)
{
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

juce::AudioBuffer<float> makeSine(int channels, int samples, double sr, float amplitude)
{
    juce::AudioBuffer<float> b(channels, samples);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < samples; ++i)
            b.setSample(ch, i, amplitude * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr)));
    return b;
}

void run(MixCoreProcessor& p, juce::AudioBuffer<float>& audio, int block)
{
    juce::MidiBuffer midi;
    for (int pos = 0; pos < audio.getNumSamples(); pos += block)
    {
        const int n = juce::jmin(block, audio.getNumSamples() - pos);
        juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), audio.getNumChannels(), pos, n);
        p.processBlock(view, midi);
    }
}

} // namespace

TEST_CASE("Every engine setting has a host parameter with matching default and range", "[plugin][params]")
{
    JuceEnv env;
    MixCoreProcessor p;
    const gain::SmartGainSettings defaults;
    for (const auto& spec : floatParamSpecs())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*>(p.state().getParameter(spec.id));
        REQUIRE(param != nullptr);
        INFO(spec.id);
        CHECK(param->convertFrom0to1(param->getDefaultValue()) == Approx(defaults.*spec.member).margin(spec.step));
        CHECK(param->getNormalisableRange().start == Approx(spec.min));
        CHECK(param->getNormalisableRange().end == Approx(spec.max));
    }
    const auto current = p.currentSettings();
    for (const auto& spec : floatParamSpecs())
        CHECK(current.*spec.member == Approx(defaults.*spec.member).margin(spec.step * 0.5f));
    for (const auto& spec : boolParamSpecs())
        CHECK(current.*spec.member == defaults.*spec.member);
    CHECK(p.getBypassParameter() == p.state().getParameter(ids::bypass));
    CHECK_FALSE(p.state().getParameter(ids::peakGuard)->isAutomatable());
}

TEST_CASE("Audio flows through the processor with static gain applied", "[plugin][audio]")
{
    JuceEnv env;
    MixCoreProcessor p;
    setParam(p, ids::staticGain, 6.0f);
    setParam(p, ids::riderStrength, 0.0f);
    p.setPlayConfigDetails(2, 2, 48000.0, 512);
    p.prepareToPlay(48000.0, 512);

    auto audio = makeSine(2, 48000, 48000.0, 0.1f);
    const auto dry = audio;
    run(p, audio, 512);
    const float g = static_cast<float>(dbToGainD(6.0));
    for (int i = 0; i < audio.getNumSamples(); i += 97)
        REQUIRE(audio.getSample(1, i) == Approx(dry.getSample(1, i) * g).margin(1e-6));

    gain::SmartGainTelemetry t;
    REQUIRE(p.readTelemetry(t));
    CHECK(t.staticGainDb == Approx(6.0f));
    CHECK(t.outputRmsDb == Approx(gainToDb(0.1f) - 3.01f + 6.0f).margin(0.1));
}

TEST_CASE("Plugin state saves and restores, and rejects corrupt data", "[plugin][state]")
{
    JuceEnv env;
    juce::MemoryBlock saved;
    {
        MixCoreProcessor a;
        setParam(a, ids::targetRms, -21.0f);
        setParam(a, ids::staticGain, 4.5f);
        setParam(a, ids::riderStrength, 0.8f);
        setParam(a, ids::riderAttack, 250.0f);
        a.state().getParameter(ids::peakGuard)->setValueNotifyingHost(1.0f);
        a.state().getParameter(ids::riderEnabled)->setValueNotifyingHost(0.0f);
        a.getStateInformation(saved);
    }
    MixCoreProcessor b;
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    const auto s = b.currentSettings();
    CHECK(s.targetRmsDb == Approx(-21.0f).margin(0.05));
    CHECK(s.staticGainDb == Approx(4.5f).margin(0.05));
    CHECK(s.riderStrength == Approx(0.8f).margin(0.01));
    CHECK(s.riderAttackMs == Approx(250.0f).margin(1.0));
    CHECK(s.peakGuardEnabled);
    CHECK_FALSE(s.riderEnabled);

    const char garbage[] = "definitely not a MixCore state";
    b.setStateInformation(garbage, sizeof(garbage));
    CHECK(b.currentSettings().staticGainDb == Approx(4.5f).margin(0.05)); // unchanged
}

TEST_CASE("Host bypass is transparent and the guard reports latency", "[plugin][bypass][latency]")
{
    JuceEnv env;
    MixCoreProcessor p;
    setParam(p, ids::staticGain, 9.0f);
    p.getBypassParameter()->setValueNotifyingHost(1.0f);
    p.setPlayConfigDetails(2, 2, 48000.0, 256);
    p.prepareToPlay(48000.0, 256);
    CHECK(p.getLatencySamples() == 0);

    auto audio = makeSine(2, 24000, 48000.0, 0.2f);
    const auto dry = audio;
    run(p, audio, 256);
    for (int i = 0; i < audio.getNumSamples(); ++i)
        REQUIRE(juce::exactlyEqual(audio.getSample(0, i), dry.getSample(0, i))); // bit-transparent

    p.state().getParameter(ids::peakGuard)->setValueNotifyingHost(1.0f);
    p.prepareToPlay(48000.0, 256);
    CHECK(p.getLatencySamples() == 96); // 2 ms at 48 kHz
}

TEST_CASE("Editor builds, lays out in both modes and renders", "[plugin][editor]")
{
    JuceEnv env;
    MixCoreProcessor p;
    p.setPlayConfigDetails(2, 2, 48000.0, 480);
    p.prepareToPlay(48000.0, 480);

    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    REQUIRE(editor != nullptr);
    CHECK(editor->getWidth() >= 880);

    p.startCalibration(3.0);
    // Feed a vocal-ish signal so the meters and history have real content.
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, 480);
    double phase = 0.0;
    // 0.5 s room tone, then phrases: quiet, loud, medium (with breaths between).
    auto amplitudeAt = [](int b) {
        const double t = b / 100.0;
        if (t < 0.5) return 0.0f;
        if (t < 3.0) return 0.035f;
        if (t < 3.4) return 0.0f;
        if (t < 6.0) return 0.25f;
        if (t < 6.4) return 0.0f;
        return 0.08f;
    };
    for (int b = 0; b < 900; ++b)
    {
        const float amp = amplitudeAt(b);
        for (int i = 0; i < 480; ++i)
        {
            phase += 2.0 * juce::MathConstants<double>::pi * 196.0 / 48000.0;
            float v = 0.0f;
            for (int k = 1; k <= 5; ++k)
                v += static_cast<float>(std::sin(k * phase) / k);
            block.setSample(0, i, amp * v + 0.0005f * (juce::Random::getSystemRandom().nextFloat() - 0.5f));
            block.setSample(1, i, block.getSample(0, i));
        }
        p.processBlock(block, midi);
        if (auto* ed = dynamic_cast<MixCoreEditor*>(editor.get()))
            ed->refresh();
    }

    const char* outDir = std::getenv("MIXCORE_SNAPSHOT_DIR");
    for (bool advanced : { false, true })
    {
        if (advanced)
            dynamic_cast<MixCoreEditor&>(*editor).setAdvanced(true);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        CHECK(image.getWidth() == editor->getWidth());
        if (outDir != nullptr)
        {
            juce::File f(juce::String(outDir) + (advanced ? "/mixcore-advanced.png" : "/mixcore-main.png"));
            f.deleteFile();
            juce::FileOutputStream stream(f);
            REQUIRE(stream.openedOk());
            juce::PNGImageFormat().writeImageToStream(image, stream);
        }
    }
}

// ------------------------------------------------------------------ Presets

namespace {
struct TempPresetDir
{
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("mixcore-presets-test", "", false);
    ~TempPresetDir() { dir.deleteRecursively(); }
};
} // namespace

TEST_CASE("Factory presets appear as host programs and load correctly", "[plugin][presets]")
{
    JuceEnv env;
    MixCoreProcessor p;
    const auto& factory = gain::factoryPresets();
    REQUIRE(p.getNumPrograms() == static_cast<int>(factory.size()));
    CHECK(p.getProgramName(0) == "Lead Vocal - Natural");
    CHECK(p.currentPresetName() == "Lead Vocal - Natural");
    CHECK_FALSE(p.isPresetModified());

    setParam(p, ids::staticGain, 6.0f); // calibrated for this singer

    int kick = -1;
    for (int i = 0; i < p.getNumPrograms(); ++i)
        if (p.getProgramName(i) == "Kick Drum")
            kick = i;
    REQUIRE(kick >= 0);
    p.setCurrentProgram(kick); // as a DAW's preset menu would
    CHECK(p.getCurrentProgram() == kick);
    CHECK(p.currentPresetName() == "Kick Drum");

    const auto s = p.currentSettings();
    const auto& expected = gain::findFactoryPreset("Kick Drum")->settings;
    CHECK_FALSE(s.riderEnabled);
    CHECK(s.detectorHpfHz == Approx(expected.detectorHpfHz).margin(0.5));
    CHECK(s.phraseHoldMs == Approx(expected.phraseHoldMs).margin(1.0));
    CHECK(s.staticGainDb == Approx(6.0f).margin(0.05)); // preset kept the calibrated static gain
    CHECK_FALSE(p.isPresetModified());

    setParam(p, ids::targetRms, -15.0f);
    CHECK(p.isPresetModified());
    setParam(p, ids::staticGain, -2.0f);
    p.loadFactoryPreset("Kick Drum");
    CHECK_FALSE(p.isPresetModified()); // static gain changes never count as "modified"

    // Every factory preset survives the trip through host parameters.
    for (const auto& fp : factory)
    {
        INFO(fp.name);
        REQUIRE(p.loadFactoryPreset(juce::String(fp.name)));
        CHECK_FALSE(p.isPresetModified());
    }
    CHECK_FALSE(p.loadFactoryPreset("Not A Preset"));
}

TEST_CASE("User presets save, list, load and delete", "[plugin][presets]")
{
    JuceEnv env;
    TempPresetDir temp;
    MixCoreProcessor p;
    p.setPresetDirectory(temp.dir);
    CHECK(p.presetManager().userPresets().isEmpty());

    p.loadFactoryPreset("Backing Vocal - Airy");
    setParam(p, ids::targetRms, -24.0f);
    setParam(p, ids::riderStrength, 0.3f);
    setParam(p, ids::staticGain, 4.0f);
    CHECK(p.isPresetModified());

    CHECK(p.saveUserPreset("  Sunday BV - Alto  ").isEmpty());
    CHECK(p.currentPresetName() == "Sunday BV - Alto");
    CHECK(p.currentPresetIsUser());
    CHECK_FALSE(p.isPresetModified());

    CHECK(p.saveUserPreset("Kick Drum").isNotEmpty()); // factory names are reserved
    CHECK(p.saveUserPreset("   ").isNotEmpty());       // empty names rejected
    CHECK(p.saveUserPreset("Sunday BV - Soprano").isEmpty());

    const auto list = p.presetManager().userPresets();
    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "Sunday BV - Alto");
    CHECK(list[0].file.hasFileExtension(PresetManager::kExtension));

    // Load into a fresh instance (e.g. another DAW project) sharing the folder.
    MixCoreProcessor q;
    q.setPresetDirectory(temp.dir);
    setParam(q, ids::staticGain, -5.0f);
    REQUIRE(q.loadUserPreset("Sunday BV - Alto"));
    const auto s = q.currentSettings();
    CHECK(s.targetRmsDb == Approx(-24.0f).margin(0.05));
    CHECK(s.riderStrength == Approx(0.3f).margin(0.01));
    CHECK(s.staticGainDb == Approx(-5.0f).margin(0.05)); // static gain not changed by loading
    CHECK(q.currentPresetIsUser());

    // Overwrite keeps one file.
    setParam(q, ids::deadband, 2.5f);
    CHECK(q.saveUserPreset("Sunday BV - Alto").isEmpty());
    CHECK(q.presetManager().userPresets().size() == 2);
    CHECK(q.presetManager().load("Sunday BV - Alto")->settings.gain.deadbandDb == Approx(2.5f).margin(0.05));

    // Damaged files are skipped, not fatal.
    temp.dir.getChildFile("broken.mixpreset").replaceWithText("<not xml");
    CHECK(q.presetManager().userPresets().size() == 2);

    CHECK(q.deleteUserPreset("Sunday BV - Alto"));
    CHECK_FALSE(q.presetManager().exists("Sunday BV - Alto"));
    CHECK(q.presetManager().userPresets().size() == 1);
    CHECK_FALSE(q.deleteUserPreset("Sunday BV - Alto"));
}

TEST_CASE("Session restore remembers the preset and its modified state", "[plugin][presets][state]")
{
    JuceEnv env;
    TempPresetDir temp;
    juce::MemoryBlock saved;
    {
        MixCoreProcessor a;
        a.setPresetDirectory(temp.dir);
        a.loadFactoryPreset("Speech - Pastor / Host");
        a.getStateInformation(saved);
    }
    MixCoreProcessor b;
    b.setPresetDirectory(temp.dir);
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(b.currentPresetName() == "Speech - Pastor / Host");
    CHECK_FALSE(b.isPresetModified());

    setParam(b, ids::riderStrength, 0.2f);
    b.getStateInformation(saved);
    MixCoreProcessor c;
    c.setPresetDirectory(temp.dir);
    c.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(c.currentPresetName() == "Speech - Pastor / Host");
    CHECK(c.isPresetModified()); // still shows * after reopening the project
}

TEST_CASE("Editor shows the preset menu with factory and user presets", "[plugin][presets][editor]")
{
    JuceEnv env;
    TempPresetDir temp;
    MixCoreProcessor p;
    p.setPresetDirectory(temp.dir);
    p.saveUserPreset("My Lead");
    p.loadFactoryPreset("Backing Vocal - Tight");
    p.setPlayConfigDetails(2, 2, 48000.0, 480);
    p.prepareToPlay(48000.0, 480);

    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto& ed = dynamic_cast<MixCoreEditor&>(*editor);
    ed.refresh();

    juce::ComboBox* box = nullptr;
    for (auto* child : ed.getChildren())
        if (auto* c = dynamic_cast<juce::ComboBox*>(child); c != nullptr && c->getNumItems() > gain::kNumSourceTypes)
            box = c; // the preset menu (the other header combo is the source type)
    REQUIRE(box != nullptr);
    CHECK(box->getText() == "Backing Vocal - Tight");
    CHECK(box->getNumItems() >= static_cast<int>(gain::factoryPresets().size()) + 1);

    juce::StringArray items;
    for (int i = 0; i < box->getNumItems(); ++i)
        items.add(box->getItemText(i));
    CHECK(items.contains("Kick Drum"));
    CHECK(items.contains("Backing Vocal - Airy"));
    CHECK(items.contains("My Lead"));

    if (const char* outDir = std::getenv("MIXCORE_SNAPSHOT_DIR"))
    {
        setParam(p, ids::riderStrength, 0.9f); // show the modified marker
        for (int i = 0; i < 12; ++i)
            ed.refresh();
        const auto image = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
        juce::File f(juce::String(outDir) + "/mixcore-presets.png");
        f.deleteFile();
        juce::FileOutputStream stream(f);
        juce::PNGImageFormat().writeImageToStream(image, stream);
    }
}

// ------------------------------------------------------------------ Gate

#include "mixcore/gate/GateProfiles.h"
#include "mixcore/strip/ChannelPresets.h"

TEST_CASE("Every gate setting has a host parameter with matching default and range", "[plugin][gate][params]")
{
    JuceEnv env;
    MixCoreProcessor p;
    const gate::GateSettings defaults;
    for (const auto& spec : gateFloatParamSpecs())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*>(p.state().getParameter(spec.id));
        REQUIRE(param != nullptr);
        INFO(spec.id);
        CHECK(param->convertFrom0to1(param->getDefaultValue()) == Approx(defaults.*spec.member).margin(spec.step));
        CHECK(param->getNormalisableRange().start == Approx(spec.min));
        CHECK(param->getNormalisableRange().end == Approx(spec.max));
    }
    for (const auto& spec : gateBoolParamSpecs())
        CHECK(p.state().getParameter(spec.id) != nullptr);
    CHECK_FALSE(p.currentChannelSettings().gate.enabled);                 // gate off by default
    CHECK_FALSE(p.state().getParameter(ids::gateLookahead)->isAutomatable()); // changes latency
    auto* ratio = p.state().getParameter(ids::gateRatio);
    CHECK(ratio->getText(1.0f, 32) == "Gate");
}

TEST_CASE("Gate processes audio in the plugin and reports lookahead latency", "[plugin][gate][audio]")
{
    JuceEnv env;
    MixCoreProcessor p;
    REQUIRE(p.loadGateProfile("Kick"));
    CHECK(p.currentChannelSettings().gate.enabled);
    setParam(p, ids::riderStrength, 0.0f);
    p.setPlayConfigDetails(2, 2, 48000.0, 256);
    p.prepareToPlay(48000.0, 256);
    CHECK(p.getLatencySamples() == 72); // 1.5 ms at 48 kHz

    // Quiet signal well under the Kick threshold: attenuated by the full 40 dB range.
    auto audio = makeSine(2, 48000, 48000.0, 0.003f);
    const auto dry = audio;
    run(p, audio, 256);
    const double outDb = 20.0 * std::log10(audio.getRMSLevel(0, 24000, 24000));
    const double inDb = 20.0 * std::log10(dry.getRMSLevel(0, 24000, 24000));
    CHECK(outDb - inDb == Approx(-40.0).margin(0.2));

    gate::GateTelemetry t;
    REQUIRE(p.readGateTelemetry(t));
    CHECK(t.enabled);
    CHECK(t.gainReductionDb == Approx(40.0f).margin(0.1));
}

TEST_CASE("Channel presets set the gate; gate profiles leave Smart Gain alone", "[plugin][gate][presets]")
{
    JuceEnv env;
    MixCoreProcessor p;
    REQUIRE(p.loadFactoryPreset("Snare Drum"));
    auto s = p.currentChannelSettings();
    CHECK(s.gate.enabled);
    CHECK(s.gate.sidechainHpfHz == Approx(gate::findGateProfile("Snare")->settings.sidechainHpfHz).margin(1.0));
    CHECK_FALSE(p.isPresetModified());

    REQUIRE(p.loadFactoryPreset("Lead Vocal - Natural"));
    s = p.currentChannelSettings();
    CHECK_FALSE(s.gate.enabled); // vocal presets load the vocal profile but leave the gate off
    CHECK(s.gate.rmsDetector);

    const auto gainBefore = p.currentSettings();
    REQUIRE(p.loadGateProfile("Vocal - Tight Gate"));
    CHECK(p.currentSettings() == gainBefore); // Smart Gain untouched
    CHECK(p.currentChannelSettings().gate.enabled);
    CHECK(p.isPresetModified()); // gate changed since the preset was loaded
}

TEST_CASE("Learn threshold works end to end in the plugin", "[plugin][gate][learn]")
{
    JuceEnv env;
    MixCoreProcessor p;
    p.loadGateProfile("Vocal - Gentle Expander");
    setParam(p, ids::gateEnabled, 0.0f);
    p.setPlayConfigDetails(1, 1, 48000.0, 480);
    p.prepareToPlay(48000.0, 480);
    p.startGateLearn(4.0);

    // Phrases at about -20 dBFS with gaps of -60 dBFS noise.
    juce::Random rng(3);
    juce::AudioBuffer<float> block(1, 480);
    juce::MidiBuffer midi;
    double phase = 0.0;
    for (int b = 0; b < 600; ++b)
    {
        const bool phrase = (b / 70) % 2 == 0;
        for (int i = 0; i < 480; ++i)
        {
            phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / 48000.0;
            const float v = phrase ? 0.14f * static_cast<float>(std::sin(phase)) : 0.0f;
            block.setSample(0, i, v + 0.001f * (rng.nextFloat() * 2.0f - 1.0f));
        }
        p.processBlock(block, midi);
    }
    REQUIRE(p.gateLearnState() == gate::LearnState::Ready);
    const auto r = p.computeGateLearnResult();
    INFO(r.summary);
    REQUIRE(r.valid);
    CHECK(r.suggestedThresholdDb > r.bleedDb);
    CHECK(r.suggestedThresholdDb < r.sourceDb);

    p.applyGateThreshold(r.suggestedThresholdDb, r.suggestedHysteresisDb);
    const auto g = p.currentChannelSettings().gate;
    CHECK(g.enabled);
    CHECK(g.thresholdDb == Approx(r.suggestedThresholdDb).margin(0.1));
}

TEST_CASE("User presets include the gate; old presets load with the gate off", "[plugin][gate][presets]")
{
    JuceEnv env;
    TempPresetDir temp;
    MixCoreProcessor p;
    p.setPresetDirectory(temp.dir);
    p.loadFactoryPreset("Kick Drum");
    setParam(p, ids::gateThreshold, -33.0f);
    REQUIRE(p.saveUserPreset("Church Kick").isEmpty());

    MixCoreProcessor q;
    q.setPresetDirectory(temp.dir);
    REQUIRE(q.loadUserPreset("Church Kick"));
    CHECK(q.currentChannelSettings().gate.enabled);
    CHECK(q.currentChannelSettings().gate.thresholdDb == Approx(-33.0f).margin(0.1));

    // A preset file written by the previous version (no <Gate> element).
    temp.dir.getChildFile("Old Lead.mixpreset")
        .replaceWithText("<?xml version=\"1.0\"?><MixCorePreset version=\"1\" module=\"SmartGain\" name=\"Old Lead\">"
                         "<Settings>targetRmsDb=-20;riderStrength=0.6;</Settings></MixCorePreset>");
    setParam(q, ids::gateEnabled, 1.0f);
    REQUIRE(q.loadUserPreset("Old Lead"));
    CHECK(q.currentSettings().targetRmsDb == Approx(-20.0f).margin(0.05));
    CHECK_FALSE(q.currentChannelSettings().gate.enabled);
}

TEST_CASE("Gate page renders", "[plugin][gate][editor]")
{
    JuceEnv env;
    MixCoreProcessor p;
    p.loadFactoryPreset("Snare Drum");
    p.setPlayConfigDetails(1, 1, 48000.0, 480);
    p.prepareToPlay(48000.0, 480);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto& ed = dynamic_cast<MixCoreEditor&>(*editor);
    ed.setPage(MixCoreEditor::Page::Gate);

    // Snare hits over hi-hat-like bleed so the graph has content.
    juce::Random rng(5);
    juce::AudioBuffer<float> block(1, 480);
    juce::MidiBuffer midi;
    for (int b = 0; b < 300; ++b)
    {
        for (int i = 0; i < 480; ++i)
        {
            const int n = b * 480 + i;
            const int sinceHit = n % 24000;
            const float t = static_cast<float>(sinceHit) / 48000.0f;
            const float hit = 0.4f * std::exp(-t / 0.08f) * (0.6f * std::sin(2.0f * 3.14159f * 200.0f * t) + 0.5f * (rng.nextFloat() * 2.0f - 1.0f));
            block.setSample(0, i, hit + 0.004f * (rng.nextFloat() * 2.0f - 1.0f));
        }
        p.processBlock(block, midi);
        ed.refresh();
    }
    for (bool advanced : { false, true })
    {
        ed.setAdvanced(advanced);
        ed.refresh();
        const auto image = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
        CHECK(image.getWidth() == ed.getWidth());
        if (const char* outDir = std::getenv("MIXCORE_SNAPSHOT_DIR"))
        {
            juce::File f(juce::String(outDir) + (advanced ? "/mixcore-gate-advanced.png" : "/mixcore-gate.png"));
            f.deleteFile();
            juce::FileOutputStream stream(f);
            juce::PNGImageFormat().writeImageToStream(image, stream);
        }
    }
}

// ------------------------------------------------------------------ Drums

TEST_CASE("Every de-bleed setting has a host parameter with matching default and range", "[plugin][drum][params]")
{
    JuceEnv env;
    MixCoreProcessor p;
    const drum::DrumSettings defaults;
    for (const auto& spec : drumFloatParamSpecs())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*>(p.state().getParameter(spec.id));
        REQUIRE(param != nullptr);
        INFO(spec.id);
        CHECK(param->convertFrom0to1(param->getDefaultValue()) == Approx(defaults.*spec.member).margin(spec.step));
        CHECK(param->getNormalisableRange().start == Approx(spec.min));
        CHECK(param->getNormalisableRange().end == Approx(spec.max));
    }
    for (const auto& spec : drumBoolParamSpecs())
        CHECK(p.state().getParameter(spec.id) != nullptr);
    CHECK_FALSE(p.currentChannelSettings().drum.debleedEnabled); // off by default
    CHECK_FALSE(p.isPresetModified());
}

TEST_CASE("Drum presets set de-bleed; profiles leave Smart Gain and Gate alone", "[plugin][drum][presets]")
{
    JuceEnv env;
    MixCoreProcessor p;
    REQUIRE(p.loadFactoryPreset("Snare Drum"));
    auto s = p.currentChannelSettings();
    CHECK(s.drum.debleedEnabled);
    CHECK(s.drum.debleedAmount == Approx(50.0f).margin(1.0));
    CHECK_FALSE(p.isPresetModified());

    REQUIRE(p.loadFactoryPreset("Toms"));
    CHECK(p.currentChannelSettings().drum.debleedEnabled);
    REQUIRE(p.loadFactoryPreset("Kick Drum"));
    CHECK_FALSE(p.currentChannelSettings().drum.debleedEnabled);
    REQUIRE(p.loadFactoryPreset("Lead Vocal - Natural"));
    CHECK_FALSE(p.currentChannelSettings().drum.debleedEnabled);

    const auto before = p.currentChannelSettings();
    REQUIRE(p.loadDrumProfile("kick"));
    const auto after = p.currentChannelSettings();
    CHECK(after.gain == before.gain);
    CHECK(after.gate == before.gate);
    CHECK(after.drum.debleedEnabled);
    CHECK(after.drum.debleedLengthMs == Approx(40.0f).margin(1.0));
    CHECK(p.isPresetModified());
    CHECK_FALSE(p.loadDrumProfile("tuba"));
}

TEST_CASE("De-bleed runs in the plugin with no added latency", "[plugin][drum][audio]")
{
    JuceEnv env;
    MixCoreProcessor p;
    REQUIRE(p.loadFactoryPreset("Snare Drum"));
    setParam(p, ids::gateEnabled, 0.0f);
    setParam(p, ids::gateLookahead, 0.0f); // the snare gate's lookahead is the only latency
    setParam(p, ids::riderStrength, 0.0f);
    p.setPlayConfigDetails(1, 1, 48000.0, 256);
    p.prepareToPlay(48000.0, 256);
    CHECK(p.getLatencySamples() == 0); // de-bleed adds none

    // Snare-like hits twice a second over steady bright noise (cymbal wash).
    juce::Random rng(11);
    juce::AudioBuffer<float> audio(1, 4 * 48000);
    float hp1 = 0.0f, prev = 0.0f;
    for (int n = 0; n < audio.getNumSamples(); ++n)
    {
        const float t = static_cast<float>(n % 24000) / 48000.0f;
        const float hit = 0.4f * std::exp(-t / 0.08f)
                          * (0.6f * std::sin(2.0f * 3.14159f * 200.0f * t) + 0.5f * (rng.nextFloat() * 2.0f - 1.0f));
        const float white = rng.nextFloat() * 2.0f - 1.0f;
        hp1 = 0.5f * (hp1 + white - prev); // crude high-pass: bright wash
        prev = white;
        audio.setSample(0, n, hit + 0.03f * hp1);
    }
    run(p, audio, 256);
    drum::DeBleedTelemetry t;
    REQUIRE(p.readDebleedTelemetry(t));
    CHECK(t.enabled);
    CHECK(t.hitsSeen >= 4);
    CHECK(t.reductionDb > 3.0f); // last 10 ms is between hits: wash is being reduced
}

TEST_CASE("User presets include de-bleed; older presets load with it off", "[plugin][drum][presets]")
{
    JuceEnv env;
    TempPresetDir temp;
    MixCoreProcessor p;
    p.setPresetDirectory(temp.dir);
    p.loadFactoryPreset("Snare Drum");
    setParam(p, ids::drumDebleedAmount, 70.0f);
    setParam(p, ids::drumDebleedLength, 220.0f);
    REQUIRE(p.saveUserPreset("Church Snare").isEmpty());
    const auto text = temp.dir.getChildFile("Church Snare.mixpreset").loadFileAsString();
    CHECK(text.contains("version=\"4\""));
    CHECK(text.contains("<Drum>"));

    MixCoreProcessor q;
    q.setPresetDirectory(temp.dir);
    REQUIRE(q.loadUserPreset("Church Snare"));
    const auto d = q.currentChannelSettings().drum;
    CHECK(d.debleedEnabled);
    CHECK(d.debleedAmount == Approx(70.0f).margin(1.0));
    CHECK(d.debleedLengthMs == Approx(220.0f).margin(1.0));
    CHECK_FALSE(q.isPresetModified());

    // A v2 file (Smart Gain + Gate, written before de-bleed existed).
    temp.dir.getChildFile("Old Snare.mixpreset")
        .replaceWithText("<?xml version=\"1.0\"?><MixCorePreset version=\"2\" module=\"SmartGain\" name=\"Old Snare\">"
                         "<Settings>targetRmsDb=-20;</Settings><Gate>enabled=1;thresholdDb=-30;</Gate></MixCorePreset>");
    REQUIRE(q.loadUserPreset("Old Snare"));
    CHECK_FALSE(q.currentChannelSettings().drum.debleedEnabled);
    CHECK(q.currentChannelSettings().gate.enabled);
}

TEST_CASE("Drums page renders", "[plugin][drum][editor]")
{
    JuceEnv env;
    MixCoreProcessor p;
    p.loadFactoryPreset("Snare Drum");
    p.setPlayConfigDetails(1, 1, 48000.0, 480);
    p.prepareToPlay(48000.0, 480);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto& ed = dynamic_cast<MixCoreEditor&>(*editor);
    ed.setPage(MixCoreEditor::Page::Drums);

    juce::Random rng(5);
    juce::AudioBuffer<float> block(1, 480);
    juce::MidiBuffer midi;
    float prev = 0.0f, hp = 0.0f;
    for (int b = 0; b < 300; ++b)
    {
        for (int i = 0; i < 480; ++i)
        {
            const int n = b * 480 + i;
            const float t = static_cast<float>(n % 24000) / 48000.0f;
            const float hit = 0.4f * std::exp(-t / 0.08f)
                              * (0.6f * std::sin(2.0f * 3.14159f * 200.0f * t) + 0.5f * (rng.nextFloat() * 2.0f - 1.0f));
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            hp = 0.5f * (hp + white - prev);
            prev = white;
            block.setSample(0, i, hit + 0.03f * hp);
        }
        p.processBlock(block, midi);
        ed.refresh();
    }
    for (int k = 0; k < 12; ++k)
        ed.refresh(); // coach/info text updates at ~3 Hz
    const auto image = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
    CHECK(image.getWidth() == ed.getWidth());
    if (const char* outDir = std::getenv("MIXCORE_SNAPSHOT_DIR"))
    {
        juce::File f(juce::String(outDir) + "/mixcore-drums.png");
        f.deleteFile();
        juce::FileOutputStream stream(f);
        juce::PNGImageFormat().writeImageToStream(image, stream);
    }
}

TEST_CASE("Pre-rebrand presets are imported once, never overwriting", "[plugin][presets][brand]")
{
    JuceEnv env;
    TempPresetDir legacy;
    legacy.dir.createDirectory(); // before `current` picks its (distinct) non-existent name
    TempPresetDir current;
    current.dir.createDirectory();
    PresetManager oldManager(legacy.dir);
    REQUIRE(oldManager.save("Sunday Lead", {}).isEmpty());
    REQUIRE(oldManager.save("Clash", {}).isEmpty());
    current.dir.getChildFile("Clash.mixpreset").replaceWithText("mine");

    CHECK(PresetManager::importLegacyPresets(legacy.dir, current.dir) == 1);
    CHECK(PresetManager(current.dir).exists("Sunday Lead"));
    CHECK(current.dir.getChildFile("Clash.mixpreset").loadFileAsString() == "mine"); // not overwritten
    CHECK(legacy.dir.getChildFile("Sunday Lead.mixpreset").existsAsFile());             // original kept
    CHECK(PresetManager::importLegacyPresets(legacy.dir, current.dir) == 0);            // idempotent
    CHECK(PresetManager::defaultDirectory().getFullPathName().contains("VineAudio"));
}

TEST_CASE("Plugin reports the VineAudio brand", "[plugin][brand]")
{
    JuceEnv env;
    MixCoreProcessor p;
    CHECK(juce::String(JucePlugin_Manufacturer) == "VineAudio");
    CHECK(juce::String(JucePlugin_Name) == "MixCore");
    CHECK(theme::logoImage().isValid());
    CHECK(theme::emblemImage().isValid());
}

TEST_CASE("Module power and source type in the plugin", "[plugin][modules]")
{
    JuceEnv env;
    MixCoreProcessor p;
    auto c = p.currentChannelSettings();
    CHECK(c.gainOn);                    // Smart Gain on by default
    CHECK_FALSE(c.gate.enabled);        // everything else off
    CHECK_FALSE(c.drum.debleedEnabled);
    CHECK(c.source == gain::SourceType::Vocal);
    CHECK_FALSE(p.state().getParameter(ids::sourceType)->isAutomatable());

    REQUIRE(p.loadFactoryPreset("Kick Drum"));
    CHECK(p.currentChannelSettings().source == gain::SourceType::Drum);

    // Smart Gain off: static gain is not applied.
    REQUIRE(p.loadFactoryPreset("Lead Vocal - Natural"));
    setParam(p, ids::staticGain, 9.0f);
    setParam(p, ids::smartGainOn, 0.0f);
    CHECK(p.isPresetModified());
    p.setPlayConfigDetails(1, 1, 48000.0, 256);
    p.prepareToPlay(48000.0, 256);
    auto audio = makeSine(1, 48000, 48000.0, 0.1f);
    const auto dry = audio;
    run(p, audio, 256);
    CHECK(audio.getRMSLevel(0, 24000, 24000) == Approx(dry.getRMSLevel(0, 24000, 24000)).margin(1.0e-6));
}

TEST_CASE("User presets keep source type and module power; older files default to vocal/on", "[plugin][modules][presets]")
{
    JuceEnv env;
    TempPresetDir temp;
    MixCoreProcessor p;
    p.setPresetDirectory(temp.dir);
    p.loadFactoryPreset("Snare Drum");
    setParam(p, ids::smartGainOn, 0.0f);
    REQUIRE(p.saveUserPreset("Snare no gain").isEmpty());
    const auto text = temp.dir.getChildFile("Snare no gain.mixpreset").loadFileAsString();
    CHECK(text.contains("version=\"4\""));
    CHECK(text.contains("source=\"Drum\""));

    MixCoreProcessor q;
    q.setPresetDirectory(temp.dir);
    REQUIRE(q.loadUserPreset("Snare no gain"));
    CHECK_FALSE(q.currentChannelSettings().gainOn);
    CHECK(q.currentChannelSettings().source == gain::SourceType::Drum);
    CHECK_FALSE(q.isPresetModified());

    temp.dir.getChildFile("Old.mixpreset")
        .replaceWithText("<?xml version=\"1.0\"?><MixCorePreset version=\"3\" module=\"SmartGain\" name=\"Old\">"
                         "<Settings>targetRmsDb=-20;</Settings></MixCorePreset>");
    REQUIRE(q.loadUserPreset("Old"));
    CHECK(q.currentChannelSettings().gainOn);
    CHECK(q.currentChannelSettings().source == gain::SourceType::Vocal);
}

TEST_CASE("Off modules are shown as off", "[plugin][modules][editor]")
{
    JuceEnv env;
    MixCoreProcessor p;
    p.loadFactoryPreset("Kick Drum");
    setParam(p, ids::drumDebleed, 0.0f);
    p.setPlayConfigDetails(1, 1, 48000.0, 480);
    p.prepareToPlay(48000.0, 480);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto& ed = dynamic_cast<MixCoreEditor&>(*editor);
    juce::AudioBuffer<float> block(1, 480);
    juce::MidiBuffer midi;
    for (int b = 0; b < 120; ++b)
    {
        for (int i = 0; i < 480; ++i)
        {
            const float t = static_cast<float>((b * 480 + i) % 24000) / 48000.0f;
            block.setSample(0, i, 0.5f * std::exp(-t / 0.12f) * std::sin(2.0f * 3.14159f * 60.0f * t));
        }
        p.processBlock(block, midi);
        ed.refresh();
    }
    for (auto page : { MixCoreEditor::Page::Drums, MixCoreEditor::Page::SmartGain })
    {
        ed.setPage(page);
        for (int k = 0; k < 12; ++k)
            ed.refresh();
        const auto image = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
        CHECK(image.getWidth() == ed.getWidth());
        if (const char* outDir = std::getenv("MIXCORE_SNAPSHOT_DIR"))
        {
            juce::File f(juce::String(outDir) + (page == MixCoreEditor::Page::Drums ? "/mixcore-drums-off.png" : "/mixcore-kick.png"));
            f.deleteFile();
            juce::FileOutputStream stream(f);
            juce::PNGImageFormat().writeImageToStream(image, stream);
        }
    }
}

// --------------------------------------------------------- Drum replacement

namespace {
void writeWav(const juce::File& f, float amplitude, double sr = 44100.0, int frames = 4410)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = std::make_unique<juce::FileOutputStream>(f);
    REQUIRE(stream->openedOk());
    std::unique_ptr<juce::AudioFormatWriter> w(
        wav.createWriterFor(stream.get(), sr, 1, 24, {}, 0));
    REQUIRE(w != nullptr);
    stream.release(); // owned by the writer now
    juce::AudioBuffer<float> b(1, frames);
    for (int i = 0; i < frames; ++i)
        b.setSample(0, i, amplitude * std::exp(-static_cast<float>(i) / 800.0f) * std::sin(0.05f * static_cast<float>(i)));
    w->writeFromAudioSampleBuffer(b, 0, frames);
}
} // namespace

TEST_CASE("Drum module master switch and replacement parameters", "[plugin][replace][params]")
{
    JuceEnv env;
    MixCoreProcessor p;
    const auto d = p.currentChannelSettings().drum;
    CHECK_FALSE(d.moduleOn);
    CHECK_FALSE(d.replaceEnabled);
    CHECK_FALSE(p.state().getParameter(ids::replaceLookahead)->isAutomatable());
    CHECK(p.state().getParameter(ids::replaceOriginal)->getText(0.0f, 16) == "Off");
    CHECK(p.state().getParameter(ids::replaceLength)->getText(1.0f, 16) == "Full");
    CHECK(p.kitSource().isEmpty());

    REQUIRE(p.loadFactoryPreset("Kick Drum"));
    CHECK(p.kitSource() == "starter:kick"); // drum presets bring their starter kit...
    CHECK_FALSE(p.currentChannelSettings().drum.replaceEnabled); // ...but replacement stays off
    REQUIRE(p.loadFactoryPreset("Snare Drum"));
    CHECK(p.kitSource() == "starter:snare");
    CHECK(p.currentChannelSettings().drum.moduleOn); // de-bleed preset: module on
    CHECK_FALSE(p.isPresetModified());
}

TEST_CASE("Replacement plays in the plugin, adds its latency only while on", "[plugin][replace][audio]")
{
    JuceEnv env;
    MixCoreProcessor p;
    REQUIRE(p.loadFactoryPreset("Snare Drum"));
    setParam(p, ids::gateEnabled, 0.0f);
    setParam(p, ids::gateLookahead, 0.0f);
    setParam(p, ids::smartGainOn, 0.0f);
    setParam(p, ids::drumDebleed, 0.0f);
    p.setPlayConfigDetails(1, 1, 48000.0, 256);
    p.prepareToPlay(48000.0, 256);
    CHECK(p.getLatencySamples() == 0);

    setParam(p, ids::drumReplace, 1.0f);
    setParam(p, ids::replaceOriginal, -60.0f); // pure replacement
    setParam(p, ids::replaceThreshold, -20.0f);
    p.prepareToPlay(48000.0, 256);
    CHECK(p.getLatencySamples() == 72); // 1.5 ms (Live)

    juce::AudioBuffer<float> audio(1, 48000);
    audio.clear();
    for (int k = 0; k < 4000; ++k)
        audio.setSample(0, 10000 + k, 0.5f * std::exp(-static_cast<float>(k) / 600.0f) * std::cos(0.02f * static_cast<float>(k)));
    run(p, audio, 256);
    // Silence before the hit; the starter snare from the hit on.
    CHECK(audio.getMagnitude(0, 0, 10000 + 72) < 1.0e-4f);
    CHECK(audio.getMagnitude(0, 10000 + 72, 2400) > 0.05f);
    drum::ReplaceTelemetry t;
    REQUIRE(p.readReplaceTelemetry(t));
    CHECK(t.totalHits == 1);
    CHECK(t.kitLoaded);
}

TEST_CASE("Your own samples: files and folders become a layered kit; sessions restore it", "[plugin][replace][kits]")
{
    JuceEnv env;
    TempPresetDir temp;
    temp.dir.createDirectory();
    const auto kitDir = temp.dir.getChildFile("My Snare");
    kitDir.createDirectory();
    writeWav(kitDir.getChildFile("snare_soft.wav"), 0.1f);
    writeWav(kitDir.getChildFile("snare_mid.wav"), 0.3f);
    writeWav(kitDir.getChildFile("snare_hard_1.wav"), 0.9f);
    writeWav(kitDir.getChildFile("snare_hard_2.wav"), 0.88f);
    kitDir.getChildFile("notes.txt").replaceWithText("not audio");

    MixCoreProcessor p;
    CHECK(p.loadKitFolder(kitDir).isEmpty());
    CHECK(p.kitName() == "My Snare");
    CHECK(p.kitDescription().contains("3 velocity layers"));
    CHECK(p.kitDescription().contains("(4 samples)"));

    // Save the session, restore into a fresh instance: the kit comes back.
    juce::MemoryBlock state;
    p.getStateInformation(state);
    MixCoreProcessor q;
    q.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(q.kitName() == "My Snare");
    CHECK(q.kitDescription().contains("(4 samples)"));

    // Missing files are reported, not silently ignored.
    kitDir.getChildFile("snare_mid.wav").deleteFile();
    MixCoreProcessor r;
    const auto msg = r.restoreKit(p.kitSource());
    CHECK(msg.contains("snare_mid.wav"));
    CHECK(r.kitDescription().contains("(3 samples)"));

    // Bad input.
    CHECK(p.loadKitFiles({ kitDir.getChildFile("notes.txt") }).isNotEmpty());
    CHECK(p.loadKitFolder(temp.dir.getChildFile("nothing-here")).isNotEmpty());

    // User presets remember the kit.
    p.setPresetDirectory(temp.dir.getChildFile("presets"));
    REQUIRE(p.loadStarterKit("starter:tom").isEmpty());
    REQUIRE(p.saveUserPreset("Tom replace").isEmpty());
    REQUIRE(p.loadStarterKit("starter:kick").isEmpty());
    REQUIRE(p.loadUserPreset("Tom replace"));
    CHECK(p.kitSource() == "starter:tom");
}

TEST_CASE("Older sessions with de-bleed on keep the drum module on", "[plugin][replace][state]")
{
    JuceEnv env;
    MixCoreProcessor p;
    REQUIRE(p.loadFactoryPreset("Snare Drum"));
    juce::MemoryBlock state;
    p.getStateInformation(state);
    // Remove the drumOn parameter, as in a session saved before it existed.
    auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(xml != nullptr);
    for (auto* c : xml->getChildIterator())
        if (c->getStringAttribute("id") == ids::drumOn)
        {
            xml->removeChildElement(c, true);
            break;
        }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*xml, old);
    MixCoreProcessor q;
    q.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    CHECK(q.currentChannelSettings().drum.moduleOn);
    CHECK(q.currentChannelSettings().drum.debleedEnabled);
}

TEST_CASE("Replace view renders", "[plugin][replace][editor]")
{
    JuceEnv env;
    MixCoreProcessor p;
    p.loadFactoryPreset("Snare Drum");
    setParam(p, ids::drumReplace, 1.0f);
    setParam(p, ids::replaceOriginal, -6.0f);
    setParam(p, ids::replaceThreshold, -24.0f);
    p.setPlayConfigDetails(1, 1, 48000.0, 480);
    p.prepareToPlay(48000.0, 480);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto& ed = dynamic_cast<MixCoreEditor&>(*editor);
    ed.setDrumView(true);
    juce::Random rng(9);
    juce::AudioBuffer<float> block(1, 480);
    juce::MidiBuffer midi;
    const float amps[] = { 0.2f, 0.45f, 0.9f, 0.3f, 0.7f, 0.15f };
    for (int b = 0; b < 300; ++b)
    {
        for (int i = 0; i < 480; ++i)
        {
            const int n = b * 480 + i;
            const int hit = n / 12000;
            const float t = static_cast<float>(n % 12000) / 48000.0f;
            const float a = amps[hit % 6];
            block.setSample(0, i, a * std::exp(-t / 0.07f) * (0.6f * std::sin(2.0f * 3.14159f * 190.0f * t) + 0.4f * (rng.nextFloat() * 2.0f - 1.0f))
                                      + 0.004f * (rng.nextFloat() * 2.0f - 1.0f));
        }
        p.processBlock(block, midi);
        ed.refresh();
    }
    for (int k = 0; k < 12; ++k)
        ed.refresh();
    const auto image = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
    CHECK(image.getWidth() == ed.getWidth());
    drum::ReplaceTelemetry t;
    p.readReplaceTelemetry(t);
    CHECK(t.totalHits >= 10);
    if (const char* outDir = std::getenv("MIXCORE_SNAPSHOT_DIR"))
    {
        juce::File f(juce::String(outDir) + "/mixcore-replace.png");
        f.deleteFile();
        juce::FileOutputStream stream(f);
        juce::PNGImageFormat().writeImageToStream(image, stream);
    }
}
