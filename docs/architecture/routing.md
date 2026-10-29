# Routing

**Status: not implemented.** Routing belongs to Milestones 2 and 7 (audio
engine and standalone mixer). Today MixCore is a single insert processor:
mono→mono or stereo→stereo (`isBusesLayoutSupported`), with stereo-linked gain.
The standalone build uses JUCE's standalone wrapper, which provides audio
device selection but no multi-channel routing.

## Requirements carried forward (from the product spec)

* Mono and stereo channels; groups and buses; master output.
* Independent stream mix and monitor mixes where the device supports them.
* Inserts, aux sends, talkback, recording outputs.
* Validate device capabilities before creating routes — never assume an
  interface exposes the same channels on every OS/driver.
* Detect and warn about feedback-prone routings (e.g. a bus routed back into
  one of its own sources; talkback into a monitor mix feeding the same mic).
* Fail safe on device loss: mute outputs rather than route to unexpected ones;
  restore the last valid routing when the device returns.

## Design direction (to be confirmed when implemented)

* Routing graph built and validated on the message thread; the audio thread
  receives an immutable, pre-allocated "render plan" swapped atomically.
* Each channel strip = ordered list of modules sharing the module interface
  pattern of `SmartGainStage` (`prepare / setSettings / process / telemetry`).
* Stream bus processed from the same channel strips (no duplicated engines),
  with its own master chain and loudness targets.
