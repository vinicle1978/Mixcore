# Test plan and results

Run everything:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure        # Linux: prefix with xvfb-run -a
```

Labels: `unit`, `realtime`, `performance`, `plugin` (e.g. `ctest -L unit`).
Timing-sensitive suites (`realtime`, `performance`) are marked `RUN_SERIAL`.

## Latest recorded run

* **Date:** 2026-10-29 · **Commit:** see `git log` · **Machine:** Linux x86_64 dev
  container, 2 vCPU Intel Xeon @ 2.10 GHz, GCC 13.3, JUCE 8.0.4, Catch2 3.7.1.
* **Result: 74 / 74 CTest tests passed** (after the gate; previously 48 / 48 with presets, 37 / 37 before) (three consecutive parallel runs), and
  again from a **fresh clone configured exactly like the Linux CI job**
  (JUCE/Catch2 fetched from GitHub, engine `-Werror`).
* Engine + tests also built with **Clang 18 / libc++ with `-Werror`**: all
  engine tests pass.
* **AddressSanitizer + UBSan:** unit suite clean. **ThreadSanitizer:** thread
  test clean.
* **Not run here:** macOS and Windows builds (no runners in this environment —
  see `.github/workflows/ci.yml`), AU validation (`auval`), VST3 validator
  (`pluginval`), real DAWs, real audio interfaces, listening tests.

## Smart Gain Stage acceptance tests (spec §7.5)

| Spec item | Test case | What is asserted | Result |
|---|---|---|---|
| RMS accuracy | *RollingPower matches a reference RMS…*, *Input and output RMS telemetry is accurate* | ≤ 0.2 dB vs reference after the window fills (measured < 1e-6 dB); silence after loud audio reads as silence | pass |
| Static calibration accuracy | *Static calibration measures the active vocal level* | median within 0.75 dB of a −27 dBFS phrase pair with gaps and room noise; recommendation = target − measured; nothing applied automatically | pass |
| (calibration safety) | *Calibration clamps large corrections and flags peak conflicts*, *…does not complete without active vocal* | ±12 dB clamp; peak conflict + safer gain; no completion on noise only; cancel | pass |
| Static/rider separation | *Static gain and rider are separate systems* | strength 0 ⇒ out = in × static exactly; rider movement never changes static; static change shifts rider target by strength × Δ | pass |
| Rider convergence | *Rider converges to the configured partial correction* | at strength 0.5 and 1.0: target matches formula to 0.01 dB; mean gain within 0.1 dB of mean target; tracking error < 0.4 dB; output level moves by rider gain | pass |
| Deadband | *Deviations inside the deadband are ignored* | rider gain exactly 0 when error ≤ 1.5 dB | pass |
| Gain-rate limits | *Rider gain respects the maximum rate of change* | max slope ≤ limit × 1.001 at 6 and 2 dB/s, and ≥ 95 % (limit exercised) | pass |
| Silence & room-noise protection | *Silence, room noise and bleed are never boosted* | digital silence; continuous noise at −65/−50/−42 dBFS never active, gain 0; gain never rises after a phrase; idle release ≤ 1 dB/s to 0; negative gain held; vocal over −42 dBFS bleed still detected | pass |
| Quiet phrase recovery | *Rider recovers on a quiet phrase after a loud one* | frozen through the gap; within 0.5 dB of new target 2.5 s into the quiet phrase | pass |
| Transient rejection | *Plosives, clicks and transients do not move the rider* | 80 Hz plosives near 0 dBFS + clicks every 0.6 s: rider excursion ≤ 0.5 dB | pass |
| Peak safety | *Peak safety* (4 sections) | rider capped at headroom with restriction reported; guard holds −3 dBFS ceiling with latency reported every tick; without guard nothing is altered but warning + coach advice raised; live guard toggle is click-free and updates latency | pass |
| Input clipping detection | *Input clipping is detected and reported* | interface-clipped sine flagged, ≥ 400 events/s, coach says it cannot be repaired; clean 0.99 sine not flagged | pass |
| Stereo-linked gain | *Stereo channels receive identical (linked) gain* | per-sample gain ratio identical on both channels (1e-5) | pass |
| Sample-rate consistency | *Rider envelope is consistent across sample rates* | rider gain at 28 time points equal within 0.15 dB at 44.1/48/88.2/96 kHz | pass |
| Block-size consistency | *Output is identical for any host block size* | bit-identical output for block sizes 4096/1/17/64/480/1023, guard off and on | pass |
| Automation stress | *Automation stress…* | 30 s of random (incl. out-of-range and NaN) settings every block, random block sizes, ±4.0 noise, NaN/Inf input: all output finite and ≤ max theoretical gain | pass |
| State save and restore | *Settings state save and restore…*, *Restored settings reproduce identical processing*, *plugin: Plugin state saves and restores…* | exact round trip; unknown/missing/invalid keys handled; restored stage output bit-identical; corrupt plugin state ignored | pass |
| Bypass behaviour | *Bypass is bit-transparent and latency-aligned*, *plugin: Host bypass…* | bit-identical when bypassed; delayed by exactly the reported latency with guard on; mid-stream engage exact after 10 ms | pass |
| Long-duration realtime performance | *perf: Ten minutes of stereo audio…* | see below | pass |
| No allocation / no locks in callback | *realtime: The audio path never allocates*, *…safe across threads* | 0 allocations, 0 frees; TSan clean; p99.9 block time < 25 % of budget | pass |
| Regression | *Smart Gain lead-vocal regression…* | per-second rider gain (±0.02 dB), output RMS (±0.05 dB) and activity match `tests/regression/reference/smart_gain_lead_vocal_v1.csv` | pass |

## Preset tests

| Test | Asserts | Result |
|---|---|---|
| Factory list well-formed | ≥ 10 presets, unique names, all four categories, values within ranges, no session values, "Natural" == engine defaults | pass |
| Apply keeps session values | static gain and bypass kept; "modified" detection ignores static gain | pass |
| Airy vs Tight behaviour | on the same quiet→loud vocal, Tight lifts and cuts > 1 dB more and moves > 1.5× faster; Airy never exceeds its 4 dB/s limit | pass |
| Kick calibrates on a groove | synthetic kick at 110 BPM over bleed: calibration completes, median within 2 dB of reference; rider never moves; output bit-identical | pass |
| Snare calibrates on backbeats | snare on 2 and 4 at 100 BPM: completes, within 2.5 dB | pass |
| Vocal settings can't calibrate drums | same snare groove with vocal defaults never completes (why drum presets exist) | pass |
| Live detector change | changing the detector high-pass mid-phrase causes no false phrase end | pass |
| Plugin: host programs | factory presets exposed as DAW programs; every preset survives the round trip through host parameters | pass |
| Plugin: user presets | save/list/load/overwrite/delete, factory names reserved, empty names rejected, damaged files skipped, shared across instances | pass |
| Plugin: session restore | preset name and "modified" state survive save/reopen | pass |
| Plugin: editor | preset menu lists factory and user presets; renders | pass |

## Gate tests (spec §8)

| Spec item | Test | Result |
|---|---|---|
| Attenuation | closed = −range within 0.05 dB (10/40/80 dB); open = unity | pass |
| Triggering & timing | open within attack (+2 samples); held open through hold; fully closed by detector decay + hold + release; linear-in-dB ramp | pass |
| False-trigger rate | kick gate, snare bleed 20 dB down between kicks: opens == kicks, every kick onset within 0.5 dB | pass |
| Key filter | kick key lowers snare bleed in the detector ≥ 3.5 dB; profile threshold ≥ 4 dB above keyed bleed | pass |
| Transient trigger | swelling wash never opens it, a hit does; without it the wash holds it open | pass |
| Latency | lookahead reported exactly (72 samples at 48 kHz for 1.5 ms); onset preserved (±0.05 dB) vs > 3 dB loss without | pass |
| Recovery / hysteresis | ±2 dB wobble at threshold: 1 open with 6 dB hysteresis, ≥ 10 without | pass |
| Expander curve | (thr − level) × (ratio − 1) within 0.5 dB, capped at range | pass |
| Vocal tails | phrases ±0.1 dB, first 200 ms of tails > −1 dB, noise between phrases −8 to −10 dB | pass |
| Sample rate / block size / stereo | timing within 1 ms at 44.1–96 kHz; bit-identical across block sizes; linked gain | pass |
| Bypass / off | delayed copy, exactly the reported latency | pass |
| Learn threshold | bimodal analysis, honest "no separation", silence ignored, percussive rule; end to end on kick + bleed: learned threshold opens on every kick and never on bleed | pass |
| Stress | random (incl. invalid) settings, NaN/Inf input, never adds gain | pass |
| Strip | latency = guard + lookahead; strip bypass = delayed copy; channel presets; coach (chatter, stuck closed) | pass |
| Realtime | full strip incl. learning, filter redesigns and latency changes: 0 allocations | pass |
| Plugin | gate params match engine; audio gated at −40 dB in plugin; 72-sample latency; channel presets set the gate, profiles leave Smart Gain alone; learn → apply end to end; user presets include gate; old v1 preset loads with gate off; Gate page renders | pass |

## Plugin adapter tests

| Test | Asserts | Result |
|---|---|---|
| Parameter mapping | every engine setting has a host parameter with matching default and range; bypass parameter exposed to host; peak guard not automatable | pass (found and fixed a real bug: negative-only ranges had broken skew, so Target RMS would have defaulted to −6 dBFS) |
| Audio path | static gain applied exactly through `processBlock`; telemetry reflects it | pass |
| State | save/restore of float and bool parameters; corrupt data ignored | pass |
| Bypass & latency | host bypass bit-transparent; 96-sample latency reported at 48 kHz with guard | pass |
| Editor | constructs, lays out (normal + advanced), renders with live telemetry (snapshots via `MIXCORE_SNAPSHOT_DIR`) | pass |

## Performance (measured, not a supported-load claim)

Dev container, 2 vCPU Xeon @ 2.1 GHz, Release build, single thread:

| Configuration | Realtime factor | ≈ CPU of one core |
|---|---|---|
| Stereo, 48 kHz, 256-sample blocks | ~272× | 0.37 % |
| Stereo, 48 kHz, 256, peak guard on | ~216× | 0.46 % |
| Stereo, 96 kHz, 64-sample blocks, guard on | ~119× | 0.84 % |

No maximum channel count or latency is claimed until benchmarked on documented
target hardware (Milestone 8).

## Updating the regression reference

Only after an intentional behaviour change, reviewed by ear:

```bash
MIXCORE_UPDATE_REFERENCE=1 ./build/tests/mixcore_tests "[regression]"
```

Commit the CSV with a note here. History:

* v1 (2026-10-29): initial reference, includes idle release (ADR-007).

## Listening tests

Required before the Smart Gain Stage can be called production-ready. Protocol
and results: [`listening-tests.md`](listening-tests.md). **Status: not yet performed.**
