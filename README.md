# VineAudio MixCore — intelligent live mixing & streaming engine

![VineAudio](plugins/mixcore/Resources/vineaudio_logo.png)

MixCore is made by **VineAudio**. In your DAW it appears as **VineAudio → MixCore**.

MixCore is the shared processing and intelligence engine for a live-mixing and
live-streaming platform (churches, bands, venues, streams). It is being built
milestone by milestone, starting from a reliable foundation: the **Smart Gain
Stage**.

## Status (v0.2.0)

### Working and tested
* **Smart Gain Stage** (lead-vocal profile) as a JUCE-free C++20 library:
  * input/output RMS and sample-peak meters, 4× true-peak estimate
  * static gain with **calibration** (median of active-vocal windows,
    ±12 dB recommendation, peak-conflict check, confidence) — never applied
    without the user confirming
  * **adaptive vocal rider** separate from static gain: noise-floor-relative
    activity detection, phrase hold, deadband, strength, rate limiting,
    transient rejection, idle release, freeze in silence
  * **peak safety:** input clip detection, output peak warning, preventative
    rider restriction, optional 2 ms lookahead peak guard (reported latency)
  * lock-free telemetry snapshots, Mix Coach explanations
  * **12 factory presets** (lead vocal, backing vocal airy/tight/choir, speech,
    kick, snare, toms) and **user presets** saved to disk, shared across DAWs
    ([presets guide](docs/user-guide/presets.md))
  * realtime-safe: zero allocations / locks on the audio path (tested)
* **Gate / expander** (new): source-aware gating after Smart Gain: 24 dB/oct
  key filters, peak/RMS detector, hysteresis, hold, transient trigger,
  lookahead with reported latency, **Learn threshold**, and 9 profiles
  (vocal gentle/tight, backing vocal, speech, kick, snare, toms, overheads,
  instrument). Channel presets now set Smart Gain + Gate together.
  [Gate spec](docs/dsp/gate.md)
* **Drum de-bleed** (new, Drums tab): reduces hi-hat and cymbal bleed in drum
  close mics *during* hits as well as between them. The drum body below the
  crossover is never touched; the drum's own brightness is learned from its
  hits. Zero latency (live-safe). Snare/Toms presets switch it on.
  [De-bleed spec](docs/dsp/debleed.md).
* **Drum replacement** (new, Drums tab → Replace): detects hits and plays
  samples (velocity layers, round robin, pitch, attack, length), replacing or
  blending with the original, sample-aligned. Live mode adds 1.5 ms; Studio
  6 ms. Your own WAV/AIFF files or a kit folder (auto-sorted into velocity
  layers) or the original starter kits (kick, snare, tom).
  [Replacement spec](docs/dsp/drum-replacement.md)
* **Module power**: each module (Smart Gain, Gate, Drums) has its own on/off;
  only Smart Gain is on by default.
* **MixCore plugin + standalone app** (JUCE 8): VST3, AU (macOS), Standalone,
  with host parameters, state save/restore, host bypass, latency reporting and
  an editor showing live meters, gain chain, 20 s rider history, calibration
  and Mix Coach.
* **121 automated tests** (unit, integration, regression, realtime, performance,
  plugin) — all passing on Linux; engine also passes under Clang/libc++
  `-Werror`, ASan/UBSan and TSan. Details: [`docs/testing/test-plan.md`](docs/testing/test-plan.md).

### Not done yet
* **Listening tests** with real recordings — required before the Smart Gain
  Stage is production-ready ([protocol](docs/testing/listening-tests.md)).
* macOS and Windows builds have **not been run yet**; CI is configured
  ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) but untested.
* Not validated in DAWs, with `pluginval`/`auval`, or with real interfaces.
* Installers (.pkg with AU/VST3/app, Windows .exe) are built by
  [`build.yml`](.github/workflows/build.yml) but **not yet run**; not Developer-ID signed or notarised.
* Milestones 2 and 4–8: multi-channel engine and routing, gate/EQ/compressor/
  saturation, channel registry and recommendations, master bus and loudness,
  LiveMixing/LiveStreaming workflows, standalone mixer. See the roadmap below.

## Build

Requirements: CMake ≥ 3.22, a C++20 compiler (Xcode 15+, Visual Studio 2022,
GCC 11+ or Clang 15+). JUCE 8.0.4 and Catch2 3.7.1 are downloaded
automatically on first configure (or point `MIXCORE_JUCE_PATH` /
`MIXCORE_CATCH2_PATH` at local checkouts).

**macOS (Apple Silicon + Intel universal)**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

**Windows (Visual Studio 2022, x64)**
```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

**Linux**
```bash
sudo apt install libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
  libxcomposite-dev libxext-dev libfreetype-dev libfontconfig1-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
xvfb-run -a ctest --test-dir build --output-on-failure   # GUI test needs a display
```

Engine only (fast, no JUCE): add `-DMIXCORE_BUILD_PLUGIN=OFF`.

### Installers (recommended for DAW testing)

GitHub → **Actions → Build installers → Run workflow** produces
`MixCore-<version>-macOS.pkg` and `MixCore-<version>-Windows-Setup.exe`.
The macOS job installs its own `.pkg` and validates the AU with `auval`.
Testing in Logic Pro: [`docs/user-guide/installing-on-mac.md`](docs/user-guide/installing-on-mac.md).

### Outputs and manual installation

Built plugins are in `build/plugins/mixcore/MixCore_artefacts/Release/`:

| Format | Copy to |
|---|---|
| `VST3/MixCore.vst3` | macOS `~/Library/Audio/Plug-Ins/VST3/` · Windows `C:\Program Files\Common Files\VST3\` |
| `AU/MixCore.component` (macOS) | `~/Library/Audio/Plug-Ins/Components/` |
| `Standalone/MixCore(.app/.exe)` | anywhere; choose the audio device under *Options → Audio/MIDI Settings* |

Unsigned macOS builds must be allowed in *System Settings → Privacy & Security*.

## Repository layout

```
engine/core        units (DbMath), lock-free TripleBuffer           (header-only)
engine/dsp         RMS window, true peak, lookahead guard, biquad
modules/gain       Smart Gain Stage, rider, activity, calibration, coach
modules/gate       gate/expander, key filters, threshold learning, profiles
modules/drum       drum de-bleed, drum replacement (trigger + sampler, kits), drum profiles
modules/strip      channel strip (Smart Gain -> De-bleed -> Gate -> Replacement), presets, coach
plugins/mixcore    JUCE plugin/standalone adapter and editor
tests/             unit, integration, regression, realtime, performance, plugin
docs/              architecture, DSP spec, testing, user guide
```

## Documentation

* [Architecture overview](docs/architecture/overview.md) · [Decisions](docs/architecture/decisions.md) ·
  [Realtime safety](docs/architecture/realtime-safety.md) · [Routing](docs/architecture/routing.md)
* [Smart Gain Stage DSP spec](docs/dsp/smart-gain-stage.md) · [Gate DSP spec](docs/dsp/gate.md) · [De-bleed DSP spec](docs/dsp/debleed.md) · [Drum replacement spec](docs/dsp/drum-replacement.md)
* [Test plan & results](docs/testing/test-plan.md) · [Listening tests](docs/testing/listening-tests.md)
* [User quick start](docs/user-guide/quick-start.md) · [Installing on Mac / testing in Logic](docs/user-guide/installing-on-mac.md)
* [Third-party licences](THIRD_PARTY_LICENSES.md)

## Roadmap

1. ~~Repository & architecture~~ ✔
2. Audio foundation: multi-channel engine, channel state, routing — *partly (meters, realtime checks)*
3. ~~Smart Gain Stage~~ ✔ (pending listening tests)
4. ~~Gate/expander~~ ✔, ~~drum de-bleed~~ ✔, ~~drum replacement~~ ✔ (pending listening tests), parametric EQ, compressor, saturation
5. Channel registry, shared analysis, recommendations (preview/apply/undo)
6. Master bus, loudness & true peak, tonal analysis, LiveMixing / LiveStreaming
7. Standalone live mixer: strips, buses, scenes, Mix Coach, setup wizard
8. Production hardening: DAW compatibility, long-run tests, installers, signing
