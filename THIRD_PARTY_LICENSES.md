# Third-party software

| Component | Version | Used for | Licence | Shipped in binaries? |
|---|---|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) | 8.0.4 | plugin formats, audio devices, UI | AGPLv3 **or** commercial JUCE licence | yes |
| VST3 SDK (bundled with JUCE) | as bundled by JUCE 8.0.4 | VST3 format | Steinberg VST3 licence terms (see the SDK's LICENSE file) | yes (VST3 builds) |
| [Catch2](https://github.com/catchorg/Catch2) | 3.7.1 | tests only | Boost Software License 1.0 | no |

## Before a commercial release

* **JUCE:** distributing closed-source binaries requires a commercial JUCE
  licence (otherwise the AGPLv3 applies to the whole product). Check the
  current tiers and revenue thresholds at juce.com before release.
* **VST3:** review Steinberg's current VST3 SDK licence and logo usage rules.
* **AU / macOS:** Apple Developer ID signing and notarisation are required for
  users to load the plugin without Gatekeeper warnings.

All MixCore DSP algorithms in `engine/` and `modules/` are original
implementations. No code, algorithm or UI was copied from commercial products.
