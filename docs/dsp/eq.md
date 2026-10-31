# Parametric EQ and EQ Coach

`modules/eq` (`mixcore::eq`, no JUCE). Strip position: **Smart Gain → De-bleed →
Gate → Drum replacement → EQ** (ADR-029). Off by default; zero latency.

## Bands

| # | Band | Type | Default | Range |
|---|---|---|---|---|
| 0 | HP | high-pass, 12 / 24 / 48 dB/oct | off, 80 Hz, 24 dB | 20 Hz – 20 kHz, Q 0.1 – 10 |
| 1 | LS | low shelf | 100 Hz, 0 dB | ±18 dB, Q ≤ 2 |
| 2 | 1 | bell | 250 Hz, 0 dB | ±18 dB, Q 0.1 – 10 |
| 3 | 2 | bell | 1 kHz, 0 dB | ±18 dB |
| 4 | 3 | bell | 4 kHz, 0 dB | ±18 dB |
| 5 | HS | high shelf | 8 kHz, 0 dB | ±18 dB, Q ≤ 2 |
| 6 | LP | low-pass, 12 / 24 / 48 dB/oct | off, 18 kHz, 12 dB | |

Plus an **Output** trim (±18 dB) for level-matching.

## DSP

* Biquads designed with the RBJ "Audio EQ Cookbook" formulas (public, widely
  published), computed and run in **double precision** (Direct Form I) so low
  bells at 48–96 kHz stay quiet and accurate.
* High/low-pass: 12 dB = one 2nd-order section with the band's Q (Q > 0.707
  gives a resonant bump); 24 / 48 dB = Butterworth cascades of 2 / 4 sections
  (Q ignored), so the corner is exactly −3 dB.
* **Smoothing:** frequency (log), gain and Q glide with a 20 ms time constant;
  coefficients are recomputed every 32 samples and only while something is
  moving. No zipper noise when dragging nodes live.
* **On/off:** EQ on/off and each band's on/off crossfade over 10 ms (dry vs.
  processed), never a hard switch.
* **Latency:** 0 samples, in every state.
* Realtime: no allocation, no locks; coefficient design is arithmetic only.

## Analyzer

Two `SpectrumTap`s (lock-free rings of 16 384 samples, atomic floats) hold the
signal before and after the EQ. The editor reads the newest 4 096 samples at
UI rate, takes a Hann-windowed FFT, smooths to 1/6 octave on 200 log-spaced
points and averages over time (fast up, slow down). Shaded = after the EQ;
faint line = before. The audio thread only writes samples.

## EQ Coach — Listen & suggest

Runs on the message thread (`EqListener`), never in the audio callback, and
uses no AI/ML inference — plain spectral measurements and fixed rules.

1. **Listen** (default 8 s of real playing). Frames of 8 192 samples, hopped
   by 4 096, from the EQ input tap. Frames quieter than −55 dBFS RMS are
   ignored and don't count; only frames within 20 dB of the loudest are
   averaged (so pauses and breaths don't shape the result).
2. Build the long-term spectrum in **31 one-third-octave bands** (20 Hz – 20 kHz).
3. Compare with the **expected shape for the source**: voices (vocal, speech)
   use a typical long-term voice curve; drums and instruments use flat. Every
   region is judged **against its own neighbours**, so overall tilt and level
   never trigger a suggestion.
4. Rules (at most 5 suggestions, one per band, strongest first):

| Id | Finds | Suggests (band) |
|---|---|---|
| `hpf-rumble` | energy below 63 Hz close to (voices: within 18 dB, others: 10 dB of) the 200 Hz – 1 kHz body | 24 dB/oct high-pass: speech 100 Hz, vocal 90, drum 30, instrument 50 |
| `resonance` | a 1/3-octave band ≥ 4 dB above the median of its ±1-octave neighbours (up to 2) | narrow cut (Q 4, −1.5 to −6 dB) on the nearest bell |
| `mud` | 200–500 Hz (drums: 250–600 Hz "boxiness") above its flanks | broad cut, up to −5 dB (bell 1) |
| `harsh` | 2.5–5 kHz above its neighbours (not on drums) | cut, up to −4 dB (bell 3) |
| `air` | vocal 10–16 kHz ≥ 8 dB below its 2–5 kHz presence range (relative to a typical vocal) | +2.5 dB high shelf at 10 kHz (HS) |

Each suggestion carries a title, a plain-language reason with the measured
numbers, and the exact band settings. The wording follows the channel's source
type (e.g. "the drum's ring" only on drums).

**Nothing is applied until the user presses Apply** (per suggestion, or Apply
all). Applying sets that band's parameters through the host (so undo and
automation work) and switches the EQ on (ADR-030).

## Profiles

Starting points per source (all switch the EQ on when loaded from the EQ page):
Flat, Lead Vocal, Backing Vocal, Speech, Kick, Snare, Toms, Instrument.
Channel presets load the matching profile **with the EQ off**, so choosing a
preset never changes the sound of the EQ until the user turns it on.

## Limits / not done

* The Coach's reference curves are generic. Listening tests with real
  recordings are required before relying on its suggestions.
* No dynamic EQ, no linear-phase mode (both would add complexity or latency).
* The analyzer and Coach see the channel average (mono sum); no mid/side view.
