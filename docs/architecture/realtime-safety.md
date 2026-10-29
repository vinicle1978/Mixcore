# Realtime safety

## Rules for code that runs on the audio thread

No heap allocation or deallocation · no locks or blocking waits · no file,
network, logging or console I/O · no unbounded loops · no exceptions · no
calls into the UI · no model inference. All buffers are sized in `prepare()`.

## How the Smart Gain Stage complies

| Concern | Implementation |
|---|---|
| Buffers | `std::vector`s sized in `prepare()`; `process()` only indexes them |
| Per-sample scratch | `std::array<float, 8>` on the stack (max 8 channels) |
| Median of rider history | copy of a fixed 40-element `std::array` + `nth_element` (bounded, no allocation) |
| Settings | passed by value, validated in place (`setSettings` is noexcept and allocation-free) |
| Telemetry | `TripleBuffer` — one atomic exchange per 10 ms, wait-free |
| Calibration | one atomic state word; window store pre-allocated for 30 s |
| Bad input | NaN/Inf samples replaced with 0 and counted |
| Bad parameters | clamped, non-finite → default |
| Latency change | fade-out / switch / fade-in, no reallocation |
| Denormals | plugin wraps processing in `juce::ScopedNoDenormals` |

## How it is verified

* **Allocation test** (`tests/realtime`): the test binary replaces global
  `operator new/delete`; every `setSettings` + `process` + `readTelemetry`
  call across ~9 s of audio — including calibration completing, the guard
  toggling on and off, bypass toggling and NaN input — must perform **zero**
  allocations and deallocations. Result: 0 / 0.
* **Thread test**: audio thread processes while a UI thread hammers
  `readTelemetry`, starts/cancels calibration and computes results.
  Built and run under **ThreadSanitizer: no reports.**
* **AddressSanitizer + UBSan**: whole unit suite clean.
* **Deadline**: 99.9th-percentile processing time per 256-sample stereo block
  must be < 25 % of the block duration while a reader thread is busy. Measured
  in the dev container: p99.9 0.13 ms of a 5.33 ms budget.
* **Performance**: see `docs/testing/test-plan.md`.

## Not yet verified
* Locks inside JUCE's parameter system are not on our audio path
  (we only read `std::atomic<float>*`), but host-side behaviour has not been
  profiled in real DAWs.
* Realtime-priority scheduling behaviour on macOS/Windows hosts (no hardware
  here). To be measured in Milestone 8.
