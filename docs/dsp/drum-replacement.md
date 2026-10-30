# Drum replacement — DSP specification

Module: `modules/drum` (`mixcore::drum::DrumReplacer`, `SampleKit`, `StarterKits`).
Position in the channel: Smart Gain → De-bleed → Gate → **Replacement**.
The detector listens to the signal *before* the gate, so a closed gate never
stops it seeing a hit. The samples are added *after* the gate, so the gate never
cuts their tails.

It does what well-known drum replacers do: detect each hit, measure how hard it
was, and play a sample from a velocity-layered, round-robin kit, either alone or
mixed with the original. The design, code, UI and sounds are MixCore's own.
Nothing is copied from another product, and the starter kits are synthesised,
not sampled.

## Signal flow

```
key (pre-gate) -> detector HPF (Detect Low) -> LPF (Detect High)   [24 dB/oct each]
               -> peak envelope (instant attack, 10 ms release)

hit when: envelope >= Threshold
     and  re-armed: the envelope stayed 4 dB below the threshold for 15 ms,
          or it rose 6 dB suddenly (a new hit on top of a ringing one)
     and  Retrigger time has passed since the last hit

velocity = loudest key peak during the Lookahead window after the hit
         -> norm = clamp((peak dB - Threshold) / Range, 0..1)
         -> velocity = 1 - Dynamics x (1 - norm)                (>= 0.05)

sample   = layer floor(velocity x layers) [quiet -> loud], next round robin
           gain sqrt(velocity) x Sample level; pitch, Attack fade-in, Length fade-out

output   = original delayed by Lookahead x Original level + samples
```

### Timing

The original is delayed by the lookahead. Each sample starts on the exact sample
where the hit's threshold crossing appears in the delayed original, so the
original and the sample stay in phase. Tests check this to the sample, including
when the gate's lookahead delays the audio relative to the key.

| Mode | Lookahead (= added latency) | Use |
|---|---|---|
| Live | 1.5 ms | In-ears, monitors, front of house |
| Studio | 6 ms (any value 0.5–10 ms) | Recording or mixing; more accurate velocity on slow attacks, such as some kicks |

The latency is reported to the host only while replacement is on. Switching it
on or off fades out over 5 ms, changes the latency, then fades back in.

## Parameters

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Drums On | on/off | off | Master switch for the drum module (de-bleed and replacement) |
| Replace On | on/off | off | Off = no detection, no samples, no latency, audio untouched |
| Threshold | −60…0 dBFS | −24 | Kick/Snare presets −26, Toms −28 |
| Retrigger | 5–500 ms | 40 | Minimum time between hits |
| Dynamics | 0–100 % | 100 | 0 % = every hit at full velocity |
| Range | 6–48 dB | 24 | Input range above the threshold mapped to velocity 0–1 (Advanced) |
| Sample | −40…+12 dB | 0 | Sample level |
| Original | Off (−60)…+12 dB | 0 | Off = pure replacement |
| Pitch | ±12 semitones | 0 | |
| Attack | 0–30 ms | 0 | Fade-in at the start of each sample |
| Length | 20 ms…Full | Full | Fade-out over the last ⅓ (at most 30 ms) |
| Lookahead | 0.5–10 ms | 1.5 | Not automatable, because it changes latency |
| Detect Low / High | 20–2000 Hz / 100 Hz–20 kHz | 20 / 20 k | Kick preset: High 200 Hz. Snare: Low 150 Hz. Toms: 60 Hz–2 kHz |

## Kits

* **Starter kits.** Kick, Snare and Tom, synthesised by MixCore: 3 velocity
  layers × 2 round-robin variations each. Harder layers are louder and brighter.
  The Kick, Snare and Toms channel presets load the matching starter kit;
  replacement stays off.
* **Your own samples.** Choose WAV, AIFF or FLAC files, or a folder (searched
  recursively; at most 128 files, each 30 s or shorter). MixCore measures each
  file's loudness (its loudest 10 ms RMS) and sorts the files quiet to loud.
  Files within 1.5 dB of each other become round-robin variations of one layer,
  and there are at most 16 layers. File names don't matter. Silent files are
  skipped. Unreadable files are reported.
* **Sample rate.** Samples keep their own sample rate and are resampled while
  they play (cubic Hermite), together with the pitch setting.
* **Recall.** Sessions and user presets store the kit (starter id, or file
  paths). If files have moved, MixCore loads what it can find and names the
  missing files.

## Realtime safety

* The voice pool (16 voices), trigger queue and delay lines are all fixed size.
  No allocation, locks or file I/O happen on the audio thread.
* Kits are built on the message thread and handed over through `KitExchange`,
  using an atomic pointer. A replaced kit is freed on the message thread only
  after two more audio blocks have finished, by which point the audio thread
  can no longer be using it. When the audio thread sees a new kit, it stops the
  old kit's voices.
* If more than 4 hits overlap, the oldest voice fades out over 5 ms. If all 16
  voices are busy, the oldest is stolen.
* Off and settled, the replacement path is a bit-exact pass-through with zero
  latency.

## Tests (summary)

The full table is in `docs/testing/test-plan.md`.

* Sample-exact alignment, with and without a key offset.
* One trigger per kick over bleed, with no double triggers.
* Retrigger behaviour on flams and fast rolls.
* Velocity is monotonic; Dynamics 0 gives fixed velocity.
* Layer and round-robin selection.
* Pitch ±12 semitones and conversion from a 44.1 kHz sample.
* Attack and Length.
* Block-size invariance.
* Auto layering, plus the starter kits.
* Kit swaps against a running audio thread (ThreadSanitizer/AddressSanitizer).
* Zero allocations in the strip while replacement turns on and off and its
  lookahead changes.
* In the plugin: latency, end-to-end playback, kits from files and folders,
  session and preset recall, and missing files.

## Limits (honest)

* **Velocity in Live mode.** Velocity is measured over the first 1.5 ms. That
  works well for snare and toms. For kicks with slow attacks, soft and hard hits
  can measure closer together than they sound. Studio mode, or a lower Range,
  helps.
* **Bleed.** Threshold, the detector band and Retrigger must reject bleed. A
  loud tom right next to the snare mic can still trigger the snare. Check the
  hit markers.
* **Layer choice.** Layers are chosen by loudness, not by file name. A
  deliberately quiet but "hard-hit" sample will be placed as a soft layer.
* **Cross-mic triggering.** Replacement on one channel can't trigger from
  another channel's mic (for example, kick replacement keyed from a kick-in mic
  on another channel). That needs the multi-channel engine (milestone 2), along
  with MIDI out.
* **Listening tests.** Real-drum listening tests are still required (see
  `docs/testing/listening-tests.md`).
