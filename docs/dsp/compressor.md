# Compressor and Comp Coach

`modules/comp` (`mixcore::comp`, no JUCE). The strip order is **Smart Gain → De-bleed → Gate →
Drum replacement → EQ → Compressor** (ADR-031). The compressor is off by default and adds zero latency.

## Controls

| Control | Range | Default | Notes |
|---|---|---|---|
| Threshold | −60 … 0 dBFS | −18 | Uses MixCore's level scale (a full-scale sine is −3 dBFS RMS) |
| Ratio | 1 … 20 :1 | 3 | 20:1 is shown as "limit" |
| Knee | 0 … 24 dB | 6 | Soft-knee width; 0 is a hard knee |
| Attack | 0.1 … 100 ms | 10 | |
| Release | 10 … 2000 ms | 150 | With Auto release this is the fast part |
| Auto release | on/off | off | Program-dependent release (see below) |
| Makeup | −12 … +24 dB | 0 | Ignored while Auto makeup is on |
| Auto makeup | on/off | off | Follows the average gain reduction |
| Mix | 0 … 100 % | 100 | Parallel compression |
| Sidechain HPF | on/off, 20 … 500 Hz | on, 80 Hz | Filters the detector only, never the audio |
| Detector | Peak / RMS | RMS | Peak reacts to transients (drums); RMS (10 ms) follows loudness |

## DSP

1. **Sidechain.** The detector hears each channel through an optional 12 dB/oct Butterworth
   high-pass. The loudest channel drives the gain, and the same gain is applied to every channel
   (stereo link).
2. **Detector.** Peak mode uses the sample peak in dB. RMS mode uses a one-pole mean square with a
   10 ms time constant, in dB.
3. **Gain computer.** A soft-knee static curve in the log domain, from Giannoulis, Massberg &
   Reiss, JAES 2012. One function, `gainChangeDb`, is shared by the DSP, the UI's transfer curve and
   the Coach, so all three agree.
4. **Smoothing.** Smoothing happens in dB: the attack coefficient applies while reduction is
   growing and the release coefficient while it is shrinking.
5. **Auto release.** A second "slow" branch runs alongside the main one:
   - Its attack is 300 ms, so it only builds up during sustained compression.
   - Its release is max(600 ms, 5 × Release).
   - The compressor applies whichever of the two branches is reducing more.
   - Result: after a short peak it releases at the fast rate; after a long loud passage it lets go
     slowly, which means less pumping. In the tests, a 3 s loud passage still had more than 5× the
     reduction of a 20 ms peak 150 ms after the loud part ended.
6. **Auto makeup.** Makeup is a slow (3 s) average of the applied reduction. It only updates while
   the detector is within 10 dB of the threshold, so pauses don't inflate it, and it is clamped to
   0 … 18 dB. Switching the compressor on keeps the level about the same: within 2 dB in the
   tests. Average dB reduction and RMS loss are not identical.
7. **Mix and on/off.** The wet path has no delay, so dry/wet and the module's 10 ms on/off crossfade
   reduce to one gain per sample: `g = 1 + mix · fade · (wet − 1)`. Parallel compression therefore
   never comb-filters.
   - Off is bit-exact.
   - Mix 0 % is bit-exact.
   - Switching on or off mid-signal moves the gain smoothly.
8. **Realtime safety.** No allocation and no locks. Coefficients are recomputed only when attack,
   release or the HPF frequency change.

## Telemetry and the level tap

Every 10 ms the compressor publishes:
- the largest gain reduction in that interval, which drives the GR meter, the history and the
  COMP chip;
- the largest detector level, which drives the live dot on the transfer curve;
- the makeup being applied;
- the output RMS.

It also pushes the 10 ms RMS and peak of its *input* into a lock-free `LevelTap` (8192 entries,
about 82 s). It records this even while the compressor is off, so you can listen before switching
it on.

## Comp Coach: Listen & suggest

The Coach runs on the message thread and uses plain statistics plus the compressor's own curve, with
no ML.

1. **Listen** to 8 s of active material. 10 ms entries quieter than −55 dBFS RMS are pauses: they are
   ignored and don't count towards the 8 s. Of the rest, only entries within 30 dB of the loudest
   (99th percentile) are kept.
2. **Measure:**
   - **Loud** = 90th percentile of the detector level (RMS or peak, whichever the compressor uses).
   - **Typical** = median.
   - **Quiet** = 10th percentile.
   - **Crest** = loud peaks (90th percentile) minus loud 50 ms RMS (90th percentile).
3. **Threshold suggestion.** Each source has a target reduction on the loud parts:

   | Source | Reduction on loud parts | Ratio |
   |---|---|---|
   | Vocal | 5 dB | 3:1 |
   | Speech | 6 dB | 3.5:1 |
   | Drum | 6 dB | 4:1 |
   | Instrument | 3 dB | 2.5:1 |
   | Material already even (loud − quiet < 6 dB, not drums) | 2 dB, "glue" | 2:1 |

   - The threshold is solved from the static curve with the current knee, by bisection, and rounded
     to 0.5 dB.
   - Makeup is the average reduction the new setting would apply to the measured material. If Auto
     makeup is on, it is left on instead.
   - The reason quotes the measured levels and range. It also says if the current setting reduces
     the loud parts by more than 10 dB ("can sound squashed") or hardly at all.
4. **Attack suggestion:**
   - **Drums:** crest above 10 dB with attack under 8 ms → 15 ms, to keep the punch.
   - **Other sources:** crest above 14 dB with attack over 15 ms → 5 ms, to catch peaks.
5. **Apply.** Each suggestion stores the settings it was worked out from. Apply changes **only the
   values that differ** (as undoable host parameter changes) and switches the compressor on, so
   suggestions can be applied separately or together, in any order (ADR-032).
6. **Nothing is applied until the user presses Apply.**

The wording follows the channel's source type: phrases, sentences, hits or notes.

## Profiles

All profiles are starting points. They assume Smart Gain has brought the channel near its target
(−18 dBFS RMS by default). Channel presets load the matching profile with the compressor **off**.

| Profile | Threshold | Ratio | Knee | Attack / release | Notes |
|---|---|---|---|---|---|
| Lead Vocal | −22 | 3:1 | 6 | 8 / 120 ms | Auto release, auto makeup, HPF 100 Hz, RMS |
| Backing Vocal | −24 | 4:1 | 6 | 10 / 150 ms | Auto release, auto makeup, HPF 120 Hz, RMS |
| Speech | −24 | 3.5:1 | 8 | 10 / 200 ms | Auto release, auto makeup, HPF 120 Hz, RMS |
| Kick | −14 | 4:1 | 3 | 20 / 80 ms | Peak, no HPF, +2 dB makeup |
| Snare | −14 | 4:1 | 3 | 10 / 100 ms | Peak, HPF 80 Hz, +2 dB makeup |
| Toms | −16 | 3:1 | 4 | 15 / 150 ms | Peak, HPF 60 Hz, +2 dB makeup |
| Instrument | −20 | 2.5:1 | 8 | 15 / 200 ms | Auto release, auto makeup, HPF 60 Hz, RMS |
| Parallel Punch | −28 | 8:1 | 3 | 5 / 100 ms | Peak, auto makeup, **Mix 40 %** |

## Limits / not done

- No lookahead, so very fast peaks can pass before a 0.1 ms attack reacts. Use Smart Gain's peak
  guard for a hard ceiling.
- No external sidechain input, and no mid/side or unlinked-stereo mode.
- The Coach's targets are generic. Listening tests with real recordings are needed before relying
  on them.
