# Saturation and Sat Coach

`modules/sat` (`mixcore::sat`, no JUCE). The strip order is **Smart Gain → De-bleed → Gate →
Drum replacement → EQ → Compressor → Saturation** (ADR-033). Saturation is off by default.

## Controls

| Control | Range | Default | Notes |
|---|---|---|---|
| Type | Tape / Tube / Console / Clip | Tape | See the curves below |
| Drive | 0 … 36 dB | 6 | How hard the signal hits the curve |
| Mix | 0 … 100 % | 100 | Parallel saturation |
| Output | −24 … +12 dB | 0 | |
| Auto gain | on/off | on | Matches the saturated loudness to the dry signal |
| Quality | Live / Studio | Live | Live adds 0 latency. Studio uses 4× oversampling and adds 28 samples, but only while saturation is on. Quality is a setting, not automatable |

## Curves

Every curve has slope 1 at zero, so quiet material passes at its own level and Drive alone sets
how hard the signal hits the curve.

| Type | y = f(x) | Character |
|---|---|---|
| Tape | tanh(x) | Symmetric, so odd harmonics only, with smoothly rounded peaks |
| Tube | (tanh(x + 0.15) − tanh(0.15)) / sech²(0.15) | Asymmetric, so it adds even harmonics. At −8 dBFS peaks with no drive the 2nd harmonic is about −31 dB |
| Console | c − 4c³/27, with c = clamp(x, ±1.5) | Gentle and mostly 3rd harmonic until it flattens at ±1 |
| Clip | clamp(x, ±1) | Clean below full scale, hard and bright above it |

The same `shape()` function is used by the DSP, the curve display, the harmonics preview and the
Coach.

**Harmonic analysis (`harmonicsDb`, `thdDb`).** These functions run one period of a sine through the
curve at 512 points and take a DFT up to the 8th harmonic. In the tests, the harmonics measured
through the DSP matched this prediction within 1 dB for Tape, Tube and Console.

## DSP

1. **Drive** (smoothed over 20 ms) → curve → DC blocker (5 Hz; the Tube curve is asymmetric).
2. **Auto gain.**
   - The loudness of the dry and saturated signals is tracked with 300 ms averages, only while the
     input is above −60 dBFS.
   - The saturated signal is scaled to match the dry one, within −24 … +12 dB.
   - Just after switching on, the averages start as running means, so the level matches within a
     few milliseconds: no loudness bump.
   - Measured result: noise phrases with 24 dB of Tape drive stayed within 1.5 dB of their original
     level. Without auto gain the same drive was more than 6 dB louder.
3. **Mix.** `y = x + mix·(agc·wet − x)`, then output gain, then the 10 ms on/off crossfade.
   - Off is bit-exact.
   - Mix 0 % is bit-exact (Live).
   - Switching on or off, or moving Mix, never steps more than the saturated signal's own steepest
     edge (tested).
4. **Live quality.** Everything runs at the host rate, with zero latency. Heavy drive on bright
   material aliases. In the tests, a 7 kHz tone clipped 24 dB had aliasing at −10 dB relative to the
   tone.
5. **Studio quality.** The curve runs at 4× the host rate, through two linear-phase halfband FIR
   stages designed at start-up (Kaiser windowed, β = 8):
   - Stage 1: 47 taps, 1× ↔ 2×. Stage 2: 21 taps, 2× ↔ 4×.
   - Latency is 23 + 5 = **28 samples**, exactly an integer. In the tests the output matched the
     input delayed by 28 samples to within 1 % on a clean tone, and 15 kHz was within 0.5 dB.
   - The same 7 kHz test measured aliasing at **−39 dB**, about 29 dB less than Live.
   - The dry part of the mix runs inside the oversampled path too, so parallel mixing never
     comb-filters.
   - The dry delay line always runs, so switching Studio on is a clean time shift that the host
     compensates.
   - Switching Live ↔ Studio while on fades out, switches, then fades back in.
6. **Realtime safety.** No allocation and no locks. Fixed-size filter states for up to 8 channels.

## Telemetry

Every 10 ms the module publishes:
- the input peak;
- a saturation amount: the energy the curve adds or removes, relative to the driven signal, shown
  as a percentage on the SAT chip;
- the auto gain applied;
- the output peak.

It also pushes 10 ms RMS and peak values of its input into a `comp::LevelTap`, which keeps recording
while saturation is off.

## Sat Coach: Listen & suggest

The Coach reuses the compressor's level listener: 8 s of active material, pauses ignored, and
**loud** = 90th percentile of the peak level. It then works from the chosen curve itself:

- **Type.** Clip on a voice → suggest Tape ("Clip … sounds harsh on a vocal").
- **Drive.** It suggests the drive at which the curve adds a typical amount of harmonics on the loud
  peaks for this source, solved by bisection on `thdDb`:

  | Source | Harmonics on loud peaks |
  |---|---|
  | Vocal | −32 dB (≈ 2.5 %) |
  | Speech | −38 dB (≈ 1.3 %) |
  | Drum | −22 dB (≈ 8 %) |
  | Instrument | −28 dB (≈ 4 %) |

  It always switches Auto gain on. The reason says when the current setting is far above the target
  ("can sound distorted or harsh") or far below it, and when the peaks are already loud enough that
  no extra drive is needed.
- **Quality.** If saturation is in Live mode and heavy (current or suggested harmonics above
  −24 dB), it suggests Studio and states the 28-sample latency.

As with the other Coaches, each suggestion stores the settings it was worked out from, and Apply
changes only the values that differ. Nothing is applied until the user presses Apply.

## Profiles

Drives are set for peaks around −8 dBFS, which is what a channel looks like after Smart Gain brings
it to −18 dBFS RMS. Channel presets load the matching profile with saturation **off**. Loading a
profile on the Sat page keeps the current Live/Studio choice, because quality changes latency.

| Profile | Type | Drive | Mix | Harmonics at −8 dBFS peaks |
|---|---|---|---|---|
| Vocal Warmth | Tape | 4 dB | 100 % | ≈ 3 % |
| Vocal Grit | Tube | 15 dB | 40 % | ≈ 20 % in the wet part |
| Speech Presence | Console | 4 dB | 100 % | ≈ 1.5 % |
| Kick Weight | Tube | 9 dB | 100 % | ≈ 10 % |
| Snare Crack | Clip | 10 dB | 60 % | ≈ 10 % in the wet part |
| Toms Body | Tape | 8 dB | 100 % | ≈ 6 % |
| Instrument Warmth | Tape | 5 dB | 100 % | ≈ 4 % |
| Parallel Drive | Tube | 21 dB | 25 % | ≈ 30 % in the wet part |

These percentages are worked out from the curves, not measured by ear. Listening tests are still
required.

## Limits / not done

- **Studio passband.** Studio rolls off slightly above about 19 kHz at 48 kHz. That is the cost of a
  short, low-latency filter.
- **Live aliasing.** Live aliases at heavy drive on bright material. The Coach points this out.
- **No tone shaping.** There is no tone control or emphasis filter around the curve. Use the EQ
  before it.
- **Static curves.** The curves are memoryless: no tape hysteresis, no bias modelling and no
  frequency-dependent drive.
