#pragma once
// Static saturation curves (all with slope 1 at zero, so quiet signals pass at
// their own level) and their harmonic analysis. Shared by the DSP, the UI
// (curve and harmonics preview) and the Sat Coach.

#include "mixcore/sat/SatSettings.h"

#include <array>

namespace mixcore::sat {

/// The curve: y = f(x) for a driven sample x.
float shape(SatType type, float x) noexcept;

/// Harmonic levels of a sine through the curve, relative to the fundamental
/// (dB, index 0 = 2nd harmonic ... index 6 = 8th), for a sine of peak
/// `peakDb` (dBFS, before drive) and drive `driveDb`.
std::array<float, 7> harmonicsDb(SatType type, float peakDb, float driveDb) noexcept;

/// Total harmonic distortion (dB re fundamental, -150 when clean) for the same sine.
float thdDb(SatType type, float peakDb, float driveDb) noexcept;

/// Small drive that gives `targetThdDb` on a sine of peak `peakDb` (bisection, 0..36 dB).
float driveForThd(SatType type, float peakDb, float targetThdDb) noexcept;

} // namespace mixcore::sat
