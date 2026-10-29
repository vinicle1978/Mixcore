# Gate / expander — DSP specification

Status: **implemented and covered by automated tests; listening tests not yet performed.**
Source: `modules/gate/` (no JUCE dependency). Runs after the Smart Gain Stage
in the channel strip (`modules/strip/ChannelStrip.h`).

## What it does

One module, two behaviours, chosen by **ratio**:

| Ratio | Behaviour | Typical use |
|---|---|---|
| 20 ("Gate") | Below threshold the signal drops by the full **range** | Kick, snare, toms; tight vocal gating on loud stages |
| 1.2–19 | **Downward expander**: below threshold, attenuation = (threshold − level) × (ratio − 1), capped at range | Vocals, speech, overheads: lowers spill without "chopping" |

## Signal flow (per sample, stereo-linked)

```
x_ch ─► key HPF ─► key LPF ─► max over channels ─► detector ─► state machine ─► gain computer
                (24 dB/oct Butterworth each)        (peak / RMS)                       │
x_ch ─────────────────────────► lookahead delay ──────────────────────────────── × gain ─► y
```

* **Key filters** shape what the gate *listens to*, not what you hear. Each
  is 4th-order Butterworth (two biquads, Q 0.541 and 1.307). 20 Hz HPF /
  20 kHz LPF = off.
* **Detector**: *peak* (instant attack, 10 ms release) for drums; *RMS* (10 ms)
  for voices.
* **State machine**:
  `Closed → Open` when detector ≥ threshold (and, with *transient trigger*,
  the level jumped ≥ `transientDb` above its 50 ms average).
  `Open → Hold` when detector < threshold − hysteresis.
  `Hold → Open` if it comes back above the close threshold; `Hold → Closed`
  after `holdMs`.
* **Gain** moves in a straight line in dB: fully open in `attackMs`, fully
  closed in `releaseMs` (rate = range / time), so times mean what they say.
* **Lookahead** (0–5 ms): the detector sees audio before it is gated, so the
  gate is already open when a hit arrives. Latency = lookahead, reported to the
  host. Changing it fades out, switches, and fades in (no click).
* **Off / bypassed**: audio still passes through the same delay, so latency
  never jumps; switching crossfades over 10 ms.
* **Telemetry** (every 10 ms, lock-free): key level, threshold, close threshold,
  gain reduction, state, output RMS/peak, opens total, opens in the last second
  (chatter), time closed, learning state, latency.

## Learn threshold

Listens for 8 s while the source plays normally *including its gaps*. Per-10 ms
key levels are split into "bleed" and "source" with Otsu's method (the split
that best separates two groups, so it doesn't matter how much of the time the
source was playing).

| Source type | How the threshold is placed |
|---|---|
| Voices (RMS detector) | 40 % of the way from bleed to the typical source level |
| Drums (peak detector) | 18 dB below the hit **peaks** (90th percentile of the loud group), never within 6 dB of the bleed |

Why drums differ: with a good key filter, drum bleed is very quiet, so the
"40 %" rule lands far down the drum's own ring and the gate never closes
between hits. A test caught this.

Confidence: ≥ 15 dB separation = good; 8–15 dB = low (the message suggests a
gentler range, mic placement or transient trigger); < 8 dB = no usable
threshold, and the result says so instead of guessing. **Nothing changes until
you press Apply threshold.**

## Profiles

| Profile | Thr | Range | Ratio | Hyst | Attack | Hold | Release | Key | Detector | Transient | Lookahead |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Vocal - Gentle Expander (default, off) | −50 | 10 | 2 | 6 | 5 ms | 200 | 400 | 120 Hz–10 kHz | RMS | – | 0 |
| Vocal - Tight Gate | −45 | 20 | gate | 8 | 2 ms | 250 | 300 | 150 Hz–8 kHz | RMS | – | 0 |
| Backing Vocal | −45 | 12 | 2.5 | 6 | 5 ms | 250 | 500 | 150 Hz–8 kHz | RMS | – | 0 |
| Speech | −50 | 12 | 2 | 6 | 5 ms | 300 | 500 | 100 Hz–8 kHz | RMS | – | 0 |
| Kick | −26 | 40 | gate | 8 | 0.1 ms | 50 | 150 | 30–150 Hz | peak | 6 dB | 1.5 ms |
| Snare | −26 | 30 | gate | 8 | 0.1 ms | 80 | 200 | 150 Hz–5 kHz | peak | 6 dB | 1.5 ms |
| Toms | −28 | 30 | gate | 8 | 0.2 ms | 150 | 400 | 60 Hz–1 kHz | peak | 6 dB | 1.5 ms |
| Overheads | −40 | 8 | 1.5 | 6 | 1 ms | 100 | 500 | 200 Hz– | peak | – | 0 |
| Instrument | −50 | 15 | 3 | 6 | 2 ms | 150 | 300 | 60 Hz–12 kHz | RMS | – | 0 |

Thresholds assume the channel is calibrated first (vocals ≈ −18 dBFS RMS,
drums ≈ −22 dBFS RMS, which puts drum peaks around −8 dBFS). Drum thresholds use
the same "18 dB under the peak" rule as Learn threshold.

## Measured behaviour (from the automated tests)

* Closed attenuation equals the range to within 0.05 dB (10, 40, 80 dB); open
  gain is exactly unity.
* Opens fully within the attack time (+2 samples); stays fully open through
  the hold; reaches full range within detector decay + hold + release.
* Hysteresis 6 dB: a tone wobbling ±2 dB around the threshold opens once and
  stays open. Hysteresis 0: it chatters ≥ 10 times (documents why it matters).
* Expander follows (threshold − level) × (ratio − 1) to within 0.5 dB and
  caps at the range.
* Kick gate at 60 BPM with snare bleed 20 dB below the kick, placed between
  kicks: exactly one open per kick, none for bleed; the first 5 ms of every
  kick passes within 0.5 dB. The kick key filter lowers that snare bleed in the
  detector by ≥ 3.5 dB, and the profile threshold sits ≥ 4 dB above the keyed
  bleed.
* Transient trigger: a cymbal-like wash swelling to −15 dBFS never opens the
  gate; a hit on top does. Without it, the wash holds the gate open.
* Lookahead 1.5 ms: the first millisecond of a hit is untouched (±0.05 dB);
  without lookahead a 1 ms attack loses > 3 dB of it.
* Gentle vocal expander: phrases unchanged (±0.1 dB), first 200 ms of each
  reverb tail kept (> −1 dB), room noise between phrases lowered by 8–10 dB.
* Same timing at 44.1/48/88.2/96 kHz (within 1 ms); bit-identical output for
  any block size; identical gain on both stereo channels; never adds gain;
  survives random settings and NaN/Inf input.

## Known limitations / to validate by ear

* Synthetic drums are not real drums. The kick-vs-snare separation (~4 dB from
  the key filter) depends on how much low end the real snare bleed has; listen,
  and use Learn threshold on the actual kit.
* No dedicated bleed-reduction (spectral) processing yet; this is a gate, not a
  de-bleed tool. Planned later per the roadmap.
* The gate follows the rider: on a channel with the rider on, the gate sees
  up to ±6 dB of rider movement. Hysteresis absorbs most of this; drum presets
  have the rider off.
