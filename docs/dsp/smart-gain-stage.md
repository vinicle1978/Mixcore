# Smart Gain Stage — DSP specification

Status: **implemented and covered by automated tests; listening tests not yet performed.**
Source: `modules/gain/` (no JUCE dependency), primitives in `engine/dsp/`.

The Smart Gain Stage combines four separate mechanisms that are deliberately
kept apart so each can be reasoned about, tested and reported on its own:

| Mechanism | Changes audio? | Who changes it | Speed |
|---|---|---|---|
| Static gain | yes | the user (manual, or by confirming a calibration) | 240 dB/s click-free slew |
| Adaptive rider | yes | automatic, only while vocal is active | ≤ 6 dB/s (configurable) |
| Peak restriction | limits the rider's *upward* gain | automatic | follows the rider |
| Peak guard (optional) | yes, gain reduction at a ceiling | automatic, only when enabled | 2 ms lookahead ramp |

Static gain is **never** changed automatically. The rider never changes static gain.

## Signal flow (per sample, stereo-linked)

```
in ─► sanitise NaN/Inf ─► input meters / clip detect / detectors
   ─► × static gain × rider gain ─► [optional lookahead peak guard]
   ─► bypass crossfade (with latency-aligned dry) ─► output meters ─► out
```

All decisions use sample-count timing (control ticks every 10 ms are counted
in samples, not blocks), so output is **bit-identical for any host block size**
(tested with block sizes 1, 17, 64, 480, 1023 and 4096).

## Units and metering conventions

* Level in **dBFS RMS = 10·log10(mean of x²)**. A full-scale sine reads
  **−3.01 dBFS RMS**. MixCore does *not* add the +3 dB "AES sine" offset some
  meters use, so values can differ by 3 dB from such meters. This is why
  −18 dBFS RMS is described as a starting point, not a standard.
* Stereo/multichannel power is the mean over channels; gain is linked.
* RMS windows are exact rectangular windows (running sum, re-summed once per
  window wrap to remove floating-point drift). Accuracy vs. a reference
  computation: < 1e-6 dB (spec requires 0.2 dB).
* Sample peak = max |x|. True peak = 4× oversampled estimate (48-tap
  windowed-sinc polyphase interpolator). It is an **estimate**, labelled as such;
  not certified to ITU-R BS.1770.

## Timing constants

| Constant | Value | Notes |
|---|---|---|
| Primary RMS window | 400 ms | input/output RMS meters, calibration windows |
| Fast RMS window | 100 ms | activity detector, rider level samples |
| Control interval | 10 ms | 441 / 480 / 882 / 960 samples at 44.1 / 48 / 88.2 / 96 kHz — exact at every supported rate |
| Rider level history | 40 active ticks (400 ms) | median → robust level |
| Peak prediction window | 1 s | max of per-tick input peaks |
| Published peak window | 50 ms | so a UI polling ≥ 20 Hz never misses a peak |
| Noise-floor history | 5 s of 100 ms block minima | |

## Activity detection

Detector signal: input high-passed at 100 Hz (RBJ biquad, Q 0.707) so rumble
and handling noise do not count as vocal. Fast (100 ms) RMS of that signal is
compared to thresholds derived from a noise-floor estimate:

```
open  = max(noiseFloor + activityMargin, −70 dBFS)     activityMargin default 10 dB
close = open − 3 dB (hysteresis)

Inactive ──(> open)──► Confirming ──(> open for activityConfirm, 80 ms)──► Active
Active ──(< close)──► Hold ──(≥ close)──► Active
Hold ──(< close for phraseHold, 150 ms)──► Inactive
```

### Noise floor

Minimum statistics: the minimum of 100 ms block minima over the last 5 s; it
falls instantly and rises at ≤ 6 dB/s. It is clamped to **≤ −40 dBFS** so a
long sustained note can never be mistaken for "noise" (which would disable the
rider). Consequences, both intentional:

* Continuous room noise or instrument bleed — at any level up to −42 dBFS in
  tests — is never considered vocal and is never boosted.
* **Startup:** if the very first audio MixCore hears is already a quiet vocal
  (no silence before it), the floor initialises at that level and the vocal is
  not detected until the singer's first breath/pause (typically < 1 phrase).
  This is the conservative choice: the alternative (assume a low floor at
  startup) would let the rider boost loud continuous noise when a session opens.

## Static calibration

1. User presses **Calibrate** (control thread → atomic request; no locks).
2. Each control tick, while the vocal has been continuously Active for at least
   one full 400 ms window (so the window contains only vocal), the raw
   (pre-static-gain) 400 ms RMS is recorded. Default: 6 s of such windows.
3. When enough windows are collected the audio thread publishes `Ready`.
   Timeout after 90 s (`TimedOut`, result marked low-confidence).
4. On the UI thread: `measured = median(windows)`,
   `staticGain = target − measured`, constrained to **±12 dB recommendation
   range** (static range itself is ±18 dB).
5. Peak check: `predictedPeak = max raw input peak during calibration + recommended`.
   If above the restrict threshold (−3 dBFS), the result explains the conflict
   and offers `safeGain = restrict − maxPeak`.
6. Confidence is low when the inter-quartile range of windows exceeds 10 dB
   (e.g. verse vs. chorus) or the run timed out; the message says which.
7. **Nothing is applied until the user confirms** ("Apply suggestion" /
   "Apply safer gain"), which sets the static-gain parameter as a normal,
   undoable host parameter change.

If the input reached full scale during calibration, the result says so and
states that MixCore cannot repair clipping that happened before it.

## Adaptive rider

Level fed to the rider: **median of the last 40 active 100 ms RMS readings**
(raw input), plus the *target* static gain. The median makes the rider ignore
plosives, clicks and consonant bursts (a burst occupies < half the history), and
using the static target means a static change shifts the rider's view of the
level immediately, without the rider ever modifying static gain.

Every control tick:

```
if rider disabled:       desired = 0                       (glide back to unity)
elif Active and ≥ 8 active ticks of history:
    e  = target − level
    e' = 0                         if |e| ≤ deadband (1.5 dB)
       = e − sign(e)·deadband      otherwise              (continuous, no jump at the edge)
    desired = clamp(strength · e', −min(maxDown, range), +min(maxUp, range))
else (Confirming / Hold / Inactive):
    desired = current gain                                 (freeze — never boost silence)
    if Inactive for ≥ idleReleaseDelay (2 s) and desired > 0:
        desired = max(0, previousDesired − idleReleaseRate·10 ms)   (1 dB/s)

headroom = peakRestrict − (max input peak over last 1 s + static gain)
desired  = min(desired, max(headroom, 0))                  (preventative peak restriction)
```

Every sample: one-pole towards `desired` (attack 120 ms when gain falls,
release 500 ms when it rises), then slew-limited to `maxRate` (6 dB/s). Gain
state is kept in **double precision** — per-sample steps of ~1e-4 dB added to a
float lose ~1 % to rounding, which a test caught as a 0.13 % slew-limit overshoot.

Notes on behaviour:

* **Strength is a fraction of the error, not a ratio.** With strength 0.5 a
  vocal 6 dB under target (after the 1.5 dB deadband: 4.5 dB) receives +2.25 dB.
  Strength 1.0 corrects fully, up to `maxUp`/`maxDown`.
* **Idle release** exists because holding a +4 dB boost through a 30-second
  instrumental would leave the room noise 4 dB louder. Negative gain is held
  indefinitely, because releasing it would *raise* the noise.
* Peak restriction never pushes rider gain below 0 dB; reducing hot peaks is the
  peak guard's job, and diagnosing them is the Mix Coach's.

## Peak safety

| Mechanism | Default | Behaviour |
|---|---|---|
| Input clip detection | ≥ 3 consecutive samples ≥ −0.001 dBFS | flag held 2 s + event counter; Mix Coach explains it cannot be repaired |
| Output peak warning | −6 dBFS sample peak | flag held 1 s; no gain change |
| Peak restriction | −3 dBFS predicted peak | caps rider *upward* gain; reported as `riderRestrictionDb` |
| Peak guard (optional, off) | −3 dBFS ceiling, 2 ms lookahead | brick-wall safety limiter, no hard clipping |

There is **no always-on clipper**. With the guard off, hot output is reported,
not hidden (tested: output is bit-identical to input × gain).

### Peak guard algorithm

```
r[t] = min(1, ceiling / max_ch |x_ch[t]|)          required gain
h[t] = min(r[t−L … t])                             sliding minimum (monotonic deque)
s[t] = mean(h[t−L+1 … t])                          L-sample box smoothing
g[t] = s[t] if falling, else one-pole release (80 ms) towards s[t]
y[t] = x[t−L] · g[t]                               L = 2 ms
```

Every `h` in the averaging window covers sample `t−L`, so `s[t] ≤ r[t−L]` and
**|y| ≤ ceiling is guaranteed** (tested to 1e-6 with +12 dB bursts and isolated
3.0 spikes); gain falls along a linear ramp over L samples (no step).
Latency is exactly L samples (96 at 48 kHz) and is reported to the host. Turning
the guard on/off fades out (5 ms), switches path/latency, and fades in, so a
latency change never produces a click. Guard reduction is reported separately
from rider gain.

## Bypass

10 ms linear crossfade to the dry signal. The dry path is delayed by the
current latency, so bypass is bit-transparent once the fade completes, with or
without the guard (tested). Meters and the rider keep running while bypassed.

## Telemetry

Published every 10 ms through a wait-free triple buffer (complete snapshots, no
torn reads): input/output RMS, input/output sample peak, output true peak,
noise floor, static gain applied, rider gain and target, rider restriction,
guard reduction, robust level estimate, target error (with validity flag),
activity state, input clipping (+ event count), peak warning, restriction and
guard flags, bypass, non-finite input count, calibration state and progress,
latency, sample rate.

## Parameters

All parameters are validated (clamped; non-finite → default) before use, both
in the engine and on state load. See `SmartGainSettings.h` for the authoritative
list, ranges and defaults.

## Known limitations / to validate by ear

* The rider's level estimate wanders ~±0.2 dB on strongly modulated material
  (vibrato, tremolo), so the rider target moves slightly; measured tracking
  error ≤ 0.4 dB on the synthetic test vocal. Whether this is audible must be
  judged in listening tests.
* The detector high-pass at 100 Hz may under-detect very low male spoken voice;
  check in listening tests.
* Single source profile (lead vocal). Other profiles are future work.
