# MixCore quick start — Smart Gain Stage

MixCore keeps a lead vocal at a consistent level in two separate steps:
**static gain** (set once) and the **rider** (gentle, automatic, phrase-by-phrase).

## 1. Get the input right first

Set the preamp on your interface or mixer so the loudest singing peaks well
below 0 dBFS. If MixCore's input meter shows a red bar at the top, the input is
clipping. **No plugin can repair that**: turn the preamp down.

## 2. Insert MixCore

Put MixCore first on the vocal channel (before EQ and compression).
Mono or stereo both work; stereo channels get identical gain.

## 3. Choose a preset

Pick the closest preset from the menu at the top (Lead Vocal, Backing Vocal,
Speech or Drums). Presets set how MixCore behaves; they never change static
gain. See [presets.md](presets.md) for the full list and how to save your own.

## 4. Calibrate (once per singer)

1. Press **Calibrate** and have the singer perform at their normal level for
   about 6 seconds of actual singing. Pauses are ignored.
2. Read the result. It tells you the measured level, the suggested static
   gain, and warns if:
   * the correction is more than ±12 dB (fix the preamp instead),
   * the loudest peaks would get too hot (use **Apply safer gain**),
   * the level varied a lot (recalibrate on a steadier passage).
3. Press **Apply suggestion** or **Apply safer gain**. Nothing changes until
   you do, and you can undo it in your DAW like any parameter change.

You can always set **Static gain** by hand instead.

## 5. Let the rider work

* **Target RMS** (default −18 dBFS): the average level to aim for. MixCore
  measures RMS so that a full-scale sine reads −3 dBFS; other meters may show
  values 3 dB different. Treat −18 as a starting point.
* **Strength** (default 0.5): how much of the difference the rider corrects.
  0.5 corrects about half — natural sounding. 1.0 corrects fully (up to +4/−6 dB).
* The rider only moves while vocal is detected (blue **VOCAL ACTIVE**). In
  pauses it freezes; after ~2 s of silence any boost slowly returns to 0 dB so
  room noise is not left turned up.
* The graph shows the last 20 s of rider gain (silver white), peak-guard action (red)
  and vocal activity (blue band).

## 6. Peak safety

* The rider will never add gain that would push the loudest recent peaks above
  −3 dBFS. When that happens the RIDER chip shows "(held x.x)".
* **Peak guard** (optional) is a transparent safety limiter at −3 dBFS. It adds
  2 ms latency, which is reported to your DAW. Leave it off for in-ear monitor
  feeds where every millisecond counts; turn it on for recording or streaming.

## 7. Gate (optional)

Open the **Gate** tab. Pick a profile, press **Learn threshold** while the
source plays with its natural gaps, then **Apply threshold**. Drum presets
switch the gate on for you; vocal presets leave it off until you learn and
apply. Details: [presets.md](presets.md#the-gate).

## Modules on and off

Each module has a **power button** on its tab: Smart Gain, Gate, Drums
(de-bleed). A fresh MixCore has **only Smart Gain on**; switch the others on
when a track needs them. Drum presets switch on the gate (and de-bleed for
snare and toms) because that is what those presets are for.

A module that is off is **not processing**: the audio passes through untouched.
You can see it at a glance: its power button is grey, its tab text is dimmed,
its box in the signal chain reads **OFF**, and its page is dimmed with an
"IS OFF" banner and a **Turn on** button. You can still adjust its settings
while it is off.

**Source** (top right: Vocal, Speech, Drum, Instrument) tells MixCore what the
channel carries, so the Mix Coach and status talk about "the drum" and "between
hits" on a kick instead of "the vocal". Presets set it; it does not change the
processing.

## 8. Drums: de-bleed (optional)

On snare and tom mics, open the **Drums** tab. De-bleed turns down hi-hat and
cymbal bleed in the high end, even while the gate is open, without touching the
drum's body and with no added latency. Snare/Toms presets switch it on. See
[presets.md](presets.md#drum-de-bleed).

## 9. Mix Coach

The coach panel explains anything worth attention — what was detected, why it
matters, what to check, what to do, and whether MixCore is doing anything about
it automatically.

## Advanced

**Advanced** shows timing, activity and safety settings. Defaults are a
"lead vocal, moderate" starting profile, not universal rules.
