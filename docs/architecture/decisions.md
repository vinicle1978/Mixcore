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

## Open decisions (need product input)
* Company name, bundle ID and manufacturer code in `plugins/mixcore/CMakeLists.txt`
  are placeholders ("MixCore Audio", `com.mixcoreaudio.mixcore`, `Mxca`).
  They must be final before the first public release — changing them later
  breaks saved sessions in DAWs.
* JUCE licensing for commercial release (see `THIRD_PARTY_LICENSES.md`).
