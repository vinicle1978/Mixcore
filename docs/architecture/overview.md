# Architecture overview

Status as of v0.1.0: **Milestone 1 (repository & architecture) and
Milestone 3 (Smart Gain Stage) are implemented.** Milestone 2 is partially
covered (realtime-safety checks and meters exist inside the Smart Gain Stage;
a multi-channel audio engine, channel registry and routing do not exist yet).

## Layers

```
┌────────────────────────────────────────────────────────────────┐
│ plugins/mixcore  (JUCE)                                        │
│   PluginProcessor ── adapts host ⇄ engine (params, state,      │
│                      bypass, latency)                          │
│   PluginEditor    ── reads telemetry snapshots only            │
│   Formats: VST3, AU (macOS), Standalone (JUCE device wrapper)  │
└───────────────▲───────────────────────────────▲────────────────┘
                │ settings (atomics → struct)    │ telemetry (triple buffer)
┌───────────────┴───────────────────────────────┴────────────────┐
│ modules/strip    (mixcore::strip) ChannelStrip:                │
│                  Gain -> De-bleed -> Gate,                     │
│                  channel presets, strip-level Mix Coach        │
├────────────────────────────────────────────────────────────────┤
│ modules/gate     (mixcore::gate, no JUCE)                      │
│   NoiseGate, GateLearner, GateProfiles, GateSettings           │
├────────────────────────────────────────────────────────────────┤
│ modules/drum     (mixcore::drum, no JUCE)                      │
│   DeBleed, DrumProfiles, DrumSettings (replacement: planned)   │
├────────────────────────────────────────────────────────────────┤
│ modules/gain     (mixcore::gain, no JUCE)                      │
│   SmartGainStage   ── realtime processor                       │
│   VocalRider, ActivityDetector, NoiseFloorTracker,             │
│   StaticCalibrator, SmartGainSettings, SmartGainCoach          │
├────────────────────────────────────────────────────────────────┤
│ engine/dsp       (mixcore::dsp, no JUCE)                       │
│   RollingPower, TruePeakEstimator, LookaheadPeakGuard,         │
│   Biquad, BlockMaxWindow                                       │
├────────────────────────────────────────────────────────────────┤
│ engine/core      (header-only)                                 │
│   DbMath (units), TripleBuffer (lock-free snapshot exchange)   │
└────────────────────────────────────────────────────────────────┘
```

**Rule:** DSP, analysis and intelligence code never includes JUCE. JUCE is the
host/device/UI adapter. This keeps every algorithm testable in plain C++ and
lets the future standalone mixer, plugin and any offline tools share one engine.

## Thread model

| Thread | May call | Must not |
|---|---|---|
| Audio | `SmartGainStage::setSettings`, `process`, calibrator `onTick` (internal) | allocate, lock, log, touch files |
| Message/UI | `prepare`, `reset`, `readTelemetry` (single consumer), calibrator `requestStart/cancel/computeResult`, Mix Coach | read live DSP state |

Communication is one-directional and lock-free in both directions:

* **Parameters → audio:** JUCE `AudioProcessorValueTreeState` atomics are read
  once per block into a `SmartGainSettings` value, which the engine validates.
* **Audio → UI:** a complete `SmartGainTelemetry` snapshot every 10 ms via
  `TripleBuffer` (wait-free; reader always gets a whole, consistent snapshot).
* **Calibration control:** one atomic state word (`Idle → Requested →
  Collecting → Ready/TimedOut`); the window store is only read after `Ready`
  is observed with acquire ordering.

## Plugin instances and coordination

Plugin instances do **not** share memory. A future channel registry /
intelligence service (Milestone 5) must be optional: every instance keeps
processing correctly when it is absent. See `decisions.md` ADR-006.

## Where future modules go

| Milestone | Location |
|---|---|
| Gate/expander (M4) | `modules/gate/` — **done**, wired in via `modules/strip/ChannelStrip` |
| Drum de-bleed | `modules/drum/` — **done** (before the gate in `ChannelStrip`) |
| Drum replacement / augmentation | `modules/drum/` — next (own WAV kits, velocity layers) |
| EQ, compressor, saturation (M4) | `modules/<name>/`, same pattern, appended to `ChannelStrip` |
| Channel registry, shared analysis, recommendations (M5) | `engine/intelligence/`, `communication/` |
| Master bus, loudness, tonal analysis (M6) | `modules/master/`, `engine/analysis/` |
| Standalone live mixer (M7) | `apps/standalone/` (own JUCE app, not the plugin wrapper) |

Folders are created when there is code to put in them, not before.
