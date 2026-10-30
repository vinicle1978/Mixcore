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

## ADR-019 — De-bleed is a learned, single-band, zero-latency process
**Decision:** split at a crossover (LR4); keep the body untouched; reduce the
high band to what the drum's learned high/body ratio explains (ceiling) and to
its share against an estimated steady bleed (D/√(D²+B²)). No lookahead, no FFT.
**Why:** live use first (user choice "both, live-safe first"): zero latency and
a few operations per sample. A spectral/lookahead studio mode can be added
later. The cost: during the first milliseconds of a hit, cymbal and drum are not
separated (documented).

## ADR-020 — De-bleed sits before the gate
**Decision:** Smart Gain → De-bleed → Gate.
**Why:** de-bleed's in-hit reduction depends on measuring the raw bleed level
between hits; after a gate that level is gone. The gate also sees less cymbal
wash, so it chatters less.

## ADR-021 — Learning starts permissive, moves down fast, up slowly, rejects outliers
**Decision:** k starts at +10 dB (≈ no processing), −40 %/+10 % per hit, ignores
hits > 6 dB brighter than learned, subtracts the known steady bleed before
measuring, and only accepts hits within 12 dB of the loudest recent hit.
**Why:** tests showed (1) a ringing crash leaking into the body band was being
detected as "hits" and drove k to its maximum, and (2) with only asymmetric
rates, k crept upward over consecutive hits under a sustained crash, so
reduction weakened hit by hit. The outlier rule and bleed subtraction fixed both
(learned ratio now moves 0.5 dB under crashes on every other hit).

## ADR-022 — Drum module now; drum replacement next, in the same module
**Decision:** `modules/drum` holds de-bleed (phase 1) and will hold replacement
(phase 2: own WAV kits + an original starter kit, velocity layers, round robin,
attack/decay/pitch, mix with original; live-safe ≤ 3 ms, studio mode with more
lookahead). User preset files gained a `<Drum>` element (v3).
**Why:** user's order: "de-bleed first, then replacement". One module keeps one
Drums page and one settings schema.

## ADR-023 — Brand: VineAudio (maker) → MixCore (product)
**Decision:** company "VineAudio", bundle ID `com.vineaudio.mixcore`,
AU manufacturer code `Vnau` (plugin code `Mxcr` unchanged). Plugin, binaries and
installer file names stay "MixCore". Presets move to
`…/VineAudio/MixCore/Presets/`; presets in the old `…/MixCore/Presets/Smart Gain/`
folder are copied over automatically the first time (originals left in place).
UI palette (revised after first look): shades of blue with gradients and
silver; white text, bright silver labels for legibility. Orange marks hot levels
(above the warning level), red marks clipping, safety intervention and gain
reduction. The logo is shown recoloured silver-blue (original gold artwork in
git history).
**Why:** user's brand decision before any public release. Changing the
manufacturer code makes DAWs treat it as a new plugin, so sessions made with
the pre-rebrand test builds will show MixCore as missing; re-insert it.

## ADR-024 — Per-module power; only Smart Gain on by default; source type
**Decision:** every module has its own on/off (Smart Gain `smartGainOn`, gate
`gateEnabled`, de-bleed `drumDebleed`), shown as a power button on its tab.
Defaults: Smart Gain on, gate and de-bleed off. Off = not processing: Smart
Gain uses its latency-matched bypass (meters keep working), the gate passes the
(latency-aligned) input with unity gain, de-bleed is bit-exact pass-through; all
switch with a 10 ms crossfade. Drum channel presets still switch on the modules
those presets exist for (gate; de-bleed on snare/toms). A non-automatable
`sourceType` (Vocal/Speech/Drum/Instrument) is set by presets and saved in user
presets (file v4); it changes wording only (Mix Coach, status, calibration).
**Why:** user feedback: a vocal track should visibly not run drum processing,
and the coach called a kick "the vocal input".

## ADR-025 — Drum replacement: lookahead-aligned trigger + layered sampler
**Decision:** detect on the pre-gate signal, measure velocity over a lookahead
window (Live 1.5 ms / Studio 6 ms), delay the original by the same amount and
start the sample on the exact sample where the hit lands in the delayed
original. Latency only while replacement is on (host bypass keeps it, so
bypassing never changes latency). Samples play after the gate.
**Why:** sample-accurate phase alignment with the original is what makes
augmentation (blend) sound like one drum; live use needs a small, fixed
latency. Velocity accuracy on slow kick attacks is the documented trade-off.

## ADR-026 — Kits: auto layering by loudness; lock-free hand-over with deferred free
**Decision:** user samples are sorted by loudest-10-ms RMS and grouped into
layers (within 1.5 dB = round robin, max 16 layers); names are ignored. Kits
reach the audio thread through an atomic pointer; retired kits are freed on the
message thread after two completed audio blocks. Starter kits are synthesised
in code (no third-party samples, no licensing questions).
**Why:** users drop in whatever files they have; naming conventions differ
between libraries. The epoch rule is the simplest safe reclamation scheme for
one audio thread (tested under TSan/ASan with kits swapped every 3 ms).

## ADR-027 — Drum module master switch
**Decision:** `drumOn` powers the whole drum module (Drums tab power button);
de-bleed and replacement have their own on/off inside. Sessions/presets from
before the switch existed load with it on if de-bleed was on.
**Why:** one clear "is the drum module doing anything" control per the module
power design (ADR-024), with independent sub-processes.

## Open decisions (need product input)
* Plugin code `Mxcr` and the Windows installer AppId are kept; confirm they are
  final before the first public release.
* JUCE licensing for commercial release (see `THIRD_PARTY_LICENSES.md`).
