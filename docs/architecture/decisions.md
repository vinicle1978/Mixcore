# Architecture decision records

## ADR-001 — Engine is JUCE-free; JUCE is the adapter
**Decision:** All DSP, analysis and coaching logic lives in plain C++20 libraries
(`mixcore::core`, `mixcore::dsp`, `mixcore::gain`). JUCE is used for plugin
formats, audio devices, parameters/state and UI.
**Why:** algorithms are testable without a host or display, build in seconds,
and can be reused by the plugin, the future standalone mixer and offline tools.
**Cost:** a thin mapping layer (`Parameters.cpp`) between JUCE parameters and
the engine settings struct. A test asserts that every engine setting has a
matching host parameter with the same default and range.

## ADR-002 — Sample-count timing, not block timing
**Decision:** control ticks (10 ms) and all smoothing are counted in samples.
**Why:** identical output for every host buffer size (tested bit-exact), which
makes regression tests meaningful and behaviour predictable across DAWs.

## ADR-003 — Rider strength is "fraction of error corrected"
**Decision:** `desired = strength × (error shaped by deadband)`, clamped.
**Why:** predictable, explainable ("moderate strength corrects about half the
difference"), and gentle by construction. Strength 1.0 gives full correction.
**Alternative rejected:** integrating controller on output level — converges
fully but can wind up during long phrases and is harder to explain.

## ADR-004 — Robust level = median of active 100 ms readings
**Decision:** the rider reacts to the median of the last 400 ms of *active*
100 ms RMS readings rather than to a 400 ms rectangular RMS.
**Why:** a plosive at +20 dB for 15 ms raises a 400 ms RMS by several dB; the
median ignores it entirely. Measured excursion with plosives and clicks: ≤ 0.5 dB.

## ADR-005 — Peak guard is optional and latency-changing
**Decision:** the 2 ms lookahead guard is off by default; enabling it adds
2 ms latency, reported to the host. Its parameter is not automatable.
**Why:** live monitoring paths are latency-sensitive; safety is still provided
by peak restriction (rider never adds gain that would push predicted peaks over
−3 dBFS) and warnings. Toggling uses fade-out → switch → fade-in to avoid clicks.

## ADR-006 — No assumption of shared memory between plugin instances
**Decision:** every instance is self-sufficient. Cross-channel intelligence
(M5) will be an optional service; when unavailable, processing continues.
**Why:** hosts sandbox and may run plugins in separate processes.

## ADR-007 — Idle release of positive rider gain
**Decision:** after 2 s of inactivity, positive rider gain decays at 1 dB/s to
0 dB; negative gain is held.
**Why:** found by the regression test: holding +4 dB through a gap left the
room noise 4 dB louder. Releasing negative gain would raise noise, so it holds.

## ADR-008 — Noise floor capped at −40 dBFS
**Decision:** clamp the noise-floor estimate to ≤ −40 dBFS.
**Why:** otherwise a note sustained longer than the 5 s minimum window would be
classified as noise and the rider would stop. Trade-off documented in
`docs/dsp/smart-gain-stage.md` (startup behaviour).

## ADR-009 — Test framework: Catch2 v3; regression references are never auto-created
**Decision:** Catch2 for all tests; regression references are updated only
with `MIXCORE_UPDATE_REFERENCE=1` and committed deliberately.
**Why:** a missing reference must fail, not silently pass.

## ADR-010 — Dependency fetching
**Decision:** JUCE 8.0.4 and Catch2 3.7.1 are fetched by CMake `FetchContent`
by default, or taken from local checkouts via `MIXCORE_JUCE_PATH` /
`MIXCORE_CATCH2_PATH`. Pinned tags, no floating branches.

## ADR-011 — Presets never change static gain
**Decision:** a preset defines behaviour (target, rider, detection, safety).
Applying any preset, factory or user, keeps the session's static gain and
bypass. The "modified" marker also ignores static gain.
**Why:** static gain belongs to the source in the room (singer, mic, preamp)
and is set by calibration. Switching from "Natural" to "Controlled" mid-
rehearsal must not suddenly change the vocal level by several dB.

## ADR-012 — Factory presets live in the engine; user presets are files
**Decision:** factory presets are C++ data in `mixcore::gain` (JUCE-free,
testable, also exposed as host programs). User presets are XML `.mixpreset`
files in the per-user application-data folder, written atomically.
**Why:** factory presets are covered by automated tests; user presets are
shared across DAWs and the standalone app and are easy to back up and copy.

## ADR-013 — Drum presets: rider off, detector tuned
**Decision:** kick/snare/toms presets disable the rider and use long phrase
hold (1.2–1.5 s) plus a source-specific detector high-pass. The detector
high-pass became a parameter (`detectorHpfHz`, default 100 Hz, unchanged
behaviour for vocals).
**Why:** riding drums alters the performance; separated hits otherwise never
form a continuous "active" stretch, so calibration could not complete (tested).

## ADR-014 — One MixCore plugin, modules in a channel strip
**Decision:** the gate is a module inside the same MixCore plugin, after the
Smart Gain Stage (`ChannelStrip`). Latencies add up and are reported as one;
host bypass bypasses the whole strip as a latency-matched delayed copy.
**Why:** one insert per channel, shared presets/meters/coach, and the planned
cross-channel intelligence needs one engine per channel.

## ADR-015 — Gate = gate and expander in one module
**Decision:** ratio ≥ 20 is a hard gate; lower ratios are downward expansion,
both capped by range. Gain ramps are linear in dB at range/time.
**Why:** vocals want gentle expansion, drums want gating; one module keeps one
UI and one set of tests. Linear-in-dB ramps make "attack 1 ms" literally true.

## ADR-016 — Gate defaults to off, as a gentle vocal expander
**Decision:** default gate settings are the "Vocal - Gentle Expander" profile
with `enabled = false`. Vocal channel presets load their gate profile *off*;
drum presets switch it *on*.
**Why:** an unfitted gate on a vocal can cut words; one click plus Learn
threshold turns it on safely. It also makes "first preset == defaults" true, so
a fresh plugin is not shown as modified (a test caught this).

## ADR-017 — Learn threshold is source-aware
**Decision:** voices: 40 % between bleed and typical level; drums: 18 dB below
hit peaks. Drum profile thresholds follow the same rule (−26 for kick/snare).
**Why:** a test showed the voice rule makes drum gates stay open through the
drum's own ring when the key filter has made bleed very quiet.

## ADR-018 — 24 dB/oct key filters
**Decision:** key HPF/LPF are 4th-order Butterworth.
**Why:** with 12 dB/oct, a 200 Hz snare body passed a 150 Hz kick key almost
untouched in tests.

## Open decisions (need product input)
* Company name, bundle ID and manufacturer code in `plugins/mixcore/CMakeLists.txt`
  are placeholders ("MixCore Audio", `com.mixcoreaudio.mixcore`, `Mxca`).
  They must be final before the first public release — changing them later
  breaks saved sessions in DAWs.
* JUCE licensing for commercial release (see `THIRD_PARTY_LICENSES.md`).
