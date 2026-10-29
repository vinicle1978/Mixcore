# Listening tests — Smart Gain Stage

**Status: NOT YET PERFORMED.** The module must not be called production-ready
until every row below has a result and any failures are fixed.

Automated tests prove the maths; they cannot tell whether the rider pumps,
whether phrase starts sound late, or whether the gain moves are noticeable.

## Setup

* Real recordings, 24-bit, 48 kHz, unprocessed (no compression).
* MixCore on the vocal track, default Lead Vocal settings, calibrate once per
  singer. Compare against bypass with **matched loudness** (adjust static gain
  so bypassed and processed average levels match; otherwise louder wins).
* Listen on monitors *and* headphones. At least two listeners, one of them a
  working live engineer. Rate each item 1–5 and note timestamps of problems.
* Watch the rider history graph while listening: correlate what you hear with
  what the rider did.

## Material and what to listen for

| # | Material | Listen for | Result | Notes |
|---|---|---|---|---|
| 1 | Quiet singer (soft verse) | Lifted naturally? Room noise lifted with it? | — | |
| 2 | Loud singer (belted chorus) | Pulled down without sounding compressed or "ducked"? | — | |
| 3 | Same singer, verse → chorus → verse | Smooth transitions, no lag at section changes | — | |
| 4 | Spoken word (pastor/host, conversational) | Level consistency, no pumping on pauses, low male voice detected (100 Hz detector HPF) | — | |
| 5 | Breaths between phrases | Breaths not boosted, gain not jumping at phrase starts | — | |
| 6 | Plosives (P/B) and mic handling | No gain dips/jumps around plosives | — | |
| 7 | Room noise / HVAC between songs (30 s+) | No noise lift; idle release audible? | — | |
| 8 | Reverberant room / long tails | Tails not pumped up after phrases end | — | |
| 9 | Mic bleed: vocal mic with drums/guitar amp in the room | Bleed between phrases not boosted; vocal still detected | — | |
| 10 | Vibrato / long sustained notes (> 5 s) | Rider steady, no wobble with the vibrato | — | |
| 11 | Singer starting immediately at session load (no silence first) | How long until riding starts (expected: first breath) | — | |
| 12 | Peak guard on, hot singer | Guard transparent? Any audible limiting? | — | |
| 13 | Toggling bypass and peak guard during playback | Clicks or level jumps? | — | |

## Parameters to tune from these results

Rider attack/release, max rate, deadband, strength default, activity margin,
phrase hold, idle release delay/rate, detector high-pass frequency. Record any
default change in `docs/architecture/decisions.md` and update the regression
reference deliberately.
