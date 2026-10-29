# Drum de-bleed — DSP specification

Module: `modules/drum` (`mixcore::drum::DeBleed`), JUCE-free C++20.
Position in the channel: Smart Gain → **De-bleed** → Gate.

## What it is for

A gate removes bleed **between** hits. A crash or hi-hat that rings **over** a
hit passes straight through the open gate. De-bleed reduces that high-frequency
bleed during the hit and its ringing tail too. It also cuts the bleed between
hits, so it works alone or together with the gate.

No AI or model inference is involved. It is deterministic signal processing:
two frequency bands, envelope followers, and one number learned from the drum.

## Principle

A drum's own high end (snare wires, stick attack, tom overtones) rises and
falls with its body. Cymbal bleed does not. MixCore learns how much high end
this drum produces per unit of body, and treats any high end beyond that as
bleed.

```
low  = LR4 low-pass(x)  at crossover     drum body; never changed
high = LR4 high-pass(x) at crossover     wires/attack + cymbal bleed

bodyEnv = peak envelope of |low|          instant attack, 20 ms release
highEnv = peak envelope of |high|         instant attack, 20 ms release
allowed = max(k · bodyEnv, allowed · decay(Length))
D       = allowed · Tolerance             high end the drum explains
B       = bleed estimate                  high band, followed (150 ms) only while D < highEnv / 2
g       = min( D / highEnv ,  D / √(D² + B²) )   clamped to [−Amount·30 dB, 0 dB]
y       = low + g · high                   g smoothed in dB: opens 0.3 ms, closes 2 ms
```

* **Ceiling term** `D / highEnv`: the high band may not exceed what the drum
  explains. This reacts immediately to new bleed.
* **Share term** `D / √(D² + B²)`: keeps the drum's expected share of the high
  band. It removes known steady bleed under the drum's own ringing tail, where
  the ceiling term alone would let the bleed through.
* For multi-channel input, the envelopes are the maximum across channels and
  one gain is applied to all channels (stereo stays linked).

### Learning `k` (the drum's brightness)

* **Hit detection.** The body envelope jumps at least 6 dB above its 50 ms
  average, then a 40 ms refractory period follows. A hit only teaches `k` if
  its body peak is within 12 dB of the loudest recent hit (3 s decay). Bleed
  leaking into the body band, such as from a crash, never qualifies.
* **Measurement.** Over the first 6 ms after the onset:
  `r = own high peak / body peak`, where `own = √(highPeak² − B²)` with the
  known steady bleed removed.
* **Update rules:**
  * The estimate moves down quickly, 40 % per hit.
  * It moves up slowly, 10 % per hit.
  * Hits more than 6 dB brighter than learned are ignored, because they are a
    cymbal landing on the hit.
* **Starting point and reset.** `k` starts permissive (+10 dB re. body, which
  means almost no processing), so the first hits are never dulled. **Relearn
  drum** resets it to that value.

## Parameters

| Parameter | Range | Default | Meaning |
|---|---|---|---|
| De-bleed On | on/off | off | 10 ms crossfade; off = bit-exact pass-through |
| Amount | 0–100 % | 50 % | maximum high-band reduction = Amount × 30 dB |
| Crossover | 1–12 kHz | 5 kHz | only content above is processed |
| Length | 10–1000 ms | 150 ms | how long the drum's own high end may ring after a hit |
| Tolerance | 0–12 dB | 3 dB | extra high end allowed before any reduction |

All parameters are automatable. De-bleed adds **no latency**: there is no
lookahead and no FFT. It is safe for live in-ear and monitor paths.

### Profiles (starting points)

| Profile | Crossover | Length | Tolerance | Amount |
|---|---|---|---|---|
| Snare – Cymbal Bleed | 5 kHz | 150 ms | 3 dB | 50 % |
| Toms – Cymbal Bleed | 3 kHz | 250 ms | 3 dB | 50 % |
| Kick – Click Only | 2.5 kHz | 40 ms | 4 dB | 40 % |

Channel presets: **Snare Drum** and **Toms** switch de-bleed on. **Kick Drum**
and all voice presets leave it off.

## Trade-offs and limits (honest)

* **The attack is kept.** For the first few milliseconds of a hit, the drum's
  own high end is at its loudest. A cymbal landing at the same instant is not
  separated from it. A single-band method cannot do this without spectral
  analysis and latency. A future studio mode may add that.
* **Length and Tolerance trade reduction against the drum's own top.** In
  tests on a crash ringing over snare hits:
  * the snare profile (150 ms, 3 dB) removes about 1/3 of the crash under the
    snare's tail;
  * a tight setting (60 ms, 0 dB) removes about 3/4 of it.
  
  Both stayed within 1.5 dB on the clean snare's high end. On a real snare
  whose wires ring longer than its body, a short Length will dull the wires.
  Set it by ear.
* **Bleed below the crossover**, such as kick or toms in the snare mic, is not
  touched. That is the gate's job, using its key filter.
* **The LR4 split is magnitude-flat but not phase-flat.** With de-bleed on, the
  high band is phase-shifted relative to the dry signal; it is an all-pass.
  This is inaudible on its own, but it matters if the same mic is blended in
  parallel with a dry copy.
* **Results on synthetic signals do not guarantee real-world quality.**
  Listening tests with real multitrack drums are still required (see
  `docs/testing/listening-tests.md`).

## Telemetry (every 10 ms)

* `reductionDb`: the largest reduction in the last 10 ms.
* `bodyDb`, `highDb`: envelope peaks.
* `learnedRatioDb`: the learned high/body ratio.
* `hitsSeen`: the number of hits used for learning.
* `enabled`: whether de-bleed is on.

The Drums page shows body (teal), high band (amber) and reduction (red) over
the last 10 s.

## Realtime safety

* Fixed-size state for up to 8 channels.
* No allocation, locks or system calls in `process()` or `setSettings()`.
  Coefficients are recomputed in place only when the crossover changes.
* `resetLearning()` is an atomic flag consumed on the audio thread.
* Covered by the strip no-allocation test. That test exercises enable/disable,
  crossover redesigns and relearning.

## Phase 2 (planned): drum replacement and augmentation

The next step is a separate stage in the same drum module:

* hit detection with velocity;
* the user's own WAV kits, with velocity layers and round robin, plus a small
  original starter kit;
* attack, decay and pitch shaping, and a mix with the original.

The live-safe mode targets ≤ 3 ms latency. An optional studio mode will allow
more lookahead. This is functionality comparable to well-known drum
replacers, with its own design and UI; nothing is copied.
