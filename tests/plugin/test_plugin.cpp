// Plugin adapter tests: parameters map onto the engine, audio flows through
// the real juce::AudioProcessor, state restores, host bypass and latency are
// correct, and the editor can be constructed and rendered.

#include "PluginEditor.h"
#include "PluginProcessor.h"

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
