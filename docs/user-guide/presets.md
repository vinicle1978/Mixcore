# Smart Gain presets

Pick a preset from the menu at the top of MixCore, calibrate, then fine-tune.
Factory presets also appear in your DAW's own preset menu (in Logic: the
"Factory Default" menu at the top of the plugin window).

Presets set both modules of the channel: **Smart Gain** and the **Gate**.
Drum presets switch the gate on with a matching profile; vocal and speech
presets load the matching gate profile but leave the gate **off** (turn it on
after *Learn threshold*, see below).

**Loading a preset never changes Static Gain.** Static gain belongs to the
singer, mic and preamp in front of you and is set by calibration. A preset sets
*how* MixCore behaves: target level, how strongly and how fast it rides, how it
detects the source, and peak safety.

An asterisk after the preset name (`Lead Vocal - Natural  *`) means you have
changed something since loading it. Static gain changes don't count.

## Factory presets

| Preset | Target | Rider | Use it for |
|---|---|---|---|
| **Lead Vocal - Natural** | −18 | moderate (0.5), 6 dB/s | Most singers. Even phrases without sounding processed. |
| **Lead Vocal - Controlled** | −18 | strong (0.7), fast, ±1 dB deadband | A vocal that must stay on top of a busy band. |
| **Lead Vocal - Dynamic Singer** | −18 | 0.6, wide range (+6/−9 dB), peak guard on | Singers who go from whisper to belting. |
| **Lead Vocal - Live Stream** | −18 | 0.65, peak guard on | The vocal on a stream feed. |
| **Backing Vocal - Airy** | −21 | gentle (0.35), slow, 4 dB/s, ±2 dB deadband | Soft, breathy parts that should sit behind the lead and keep their natural movement. |
| **Backing Vocal - Tight** | −20 | firm (0.75), fast, 9 dB/s | Stacks and harmonies that must stay locked at an even level. |
| **Backing Vocal - Choir / Group** | −22 | slow (0.4), 3 dB/s | Group or choir mics: keeps the blend between voices. |
| **Speech - Pastor / Host** | −18 | 0.7, 80 Hz detector | Preaching, announcements, MCs moving on and off the mic. |
| **Speech - Stream / Podcast** | −18 | 0.75, peak guard on | Spoken word for streams and podcasts. |
| **Kick Drum** | −22 | **off** | Calibrated static gain + clip/peak monitoring. 30 Hz detector. |
| **Snare Drum** | −22 | **off** | Same, 150 Hz detector so kick bleed is ignored. |
| **Toms** | −22 | **off** | Same, calibrate on a fill. |

### "Airy" and "Tight" are about level behaviour

Smart Gain controls level, not tone. "Airy" here means *gentle and slow*:
softer parts are allowed to stay soft. "Tight" means *firm and fast*: every
line sits at the same level. The tonal side of airy (high-frequency lift, less
low-mid) comes from the EQ module, which is next on the roadmap.

### Drums: why the rider is off

Riding a kick or snare would change the drummer's dynamics. On drums, Smart
Gain is used to **calibrate a sensible static gain**, detect input clipping and
watch peaks. The drum presets also change how hits are detected, so that
calibration works on separated hits:

* **Phrase hold 1.2–1.5 s** bridges the gaps between hits. With vocal
  settings, a backbeat never looks "active" long enough to calibrate (this is
  tested).
* **Detector high-pass** matches the drum: 30 Hz for kick, 150 Hz for snare,
  60 Hz for toms.

Calibrate while the drummer plays a normal groove (not just soundcheck single
hits). Bleed reduction and gating for drums come with the gate module.

## The gate

Open the **Gate** tab (or click the GATE box in the signal chain).

1. **Calibrate Smart Gain first.** The gate's threshold is relative to the
   calibrated level.
2. Pick a profile from **Load profile...** if the channel preset didn't already
   (Vocal - Gentle Expander, Vocal - Tight Gate, Backing Vocal, Speech, Kick,
   Snare, Toms, Overheads, Instrument). This changes only the gate.
3. Press **Learn threshold (8 s)** and let the source play normally, *with* its
   gaps (a few bars of the groove, or a verse). Read the result:
   good separation, low separation (with advice), or "no usable threshold"
   (move the mic, or leave the gate off).
4. Press **Apply threshold**. This sets threshold and hysteresis and switches
   the gate on. Undo works like any parameter change.
5. Watch the graph: the blue line (what the gate hears) should jump above the
   white threshold on every hit or phrase and fall below the dashed line
   between them. Red shows how much the gate is turning the mic down.

| Want | Use |
|---|---|
| Less spill on a vocal without hearing a gate | Vocal - Gentle Expander (10 dB, slow) |
| Loud stage, lots of monitor/drum spill on vocal mics | Vocal - Tight Gate |
| Kick that ignores snare and hi-hat | Kick (keyed 30–150 Hz, transient trigger, 1.5 ms lookahead) |
| Snare that ignores kick | Snare (keyed from 150 Hz) |
| Gate opening on cymbal wash | Turn on **Transient trigger** (Advanced) |
| Gate stuttering on held notes | Raise **Hysteresis** or **Hold** (Advanced) |

Drum profiles use 1.5 ms lookahead so the attack of each hit is kept; this adds
1.5 ms latency (your DAW compensates; on a live console path it's 1.5 ms).

## Drum de-bleed

Open the **Drums** tab (or click the DE-BLEED box in the signal chain).
The **Snare Drum** and **Toms** presets switch it on; **Kick Drum** leaves it off.

1. Play a few normal hits. MixCore learns how bright this drum is (the readout
   shows "Drum brightness ... | n hits"). Until then it does almost nothing.
2. Watch the graph: blue = drum body, white = high band, red = how much high
   band is being turned down. Red should appear between hits and under the
   drum's tail when cymbals ring, and stay small on the hits themselves.
3. Adjust by ear:

| Want | Do |
|---|---|
| More cymbal removed | raise **Amount**, or shorten **Length**, or lower **Tolerance** |
| Snare wires / tom ring sound cut short | lengthen **Length** |
| Bright rim shots sound dull | raise **Tolerance** |
| Hi-hat still in the snare's "crack" | lower **Crossover** a little |
| Changed the drum, tuning or mic | press **Relearn drum** |

De-bleed adds no latency. It works best together with the gate: the gate
handles silence between hits and low-frequency bleed (kick in the snare mic);
de-bleed handles cymbals while the gate is open. Details and limits:
[`docs/dsp/debleed.md`](../dsp/debleed.md).

## Your own presets

1. Set things up the way you like.
2. Press **Save**, type a name (for example "Sunday Lead - Sarah") and press Save.
3. It appears under **My Presets** in the menu, in every project, every DAW and
   the standalone app.

* Saving again with the same name asks before replacing.
* **Delete** removes the selected user preset file (factory presets can't be
  deleted). Your current settings stay as they are.
* Factory preset names are reserved.

Your own presets are stored as small `.mixpreset` files, so you can back them
up or copy them to another computer:

| System | Folder |
|---|---|
| macOS | `~/Library/Application Support/VineAudio/MixCore/Presets/` |
| Windows | `%APPDATA%\VineAudio\MixCore\Presets\` |
| Linux | `~/.config/VineAudio/MixCore/Presets/` |

Presets you saved before the VineAudio rebrand (in `…/MixCore/Presets/Smart Gain/`)
are copied into the new folder automatically the first time MixCore opens.

User presets save the gate and de-bleed too. Presets saved by earlier versions
still load, with the newer modules off.

A user preset also records the static gain at the time you saved it (for
reference), but loading it does not change static gain, the same as factory presets.

## Status

Factory preset values are informed starting points, checked by automated tests
on synthetic vocals and drum grooves. They still need tuning by ear with real
recordings (see `docs/testing/listening-tests.md`); expect some values to
change after that.
