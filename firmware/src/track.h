#pragma once

#include <stdint.h>

#include "config.h"

// Per-peer distance history, reduced to the two numbers a driver can act on:
// how fast the gap is closing, and how long until it reaches zero.
//
// This is what UWB buys that GPS could not. Ranging at 10 Hz with ~10 cm of
// noise resolves closing speed to roughly 0.3 m/s, which makes time-to-collision
// a usable signal — and TTC is a better basis for a warning than bearing ever
// was. "3.2 m, closing at 4 m/s, 0.8 s to contact" is a complete safety
// picture; an angle is not.
//
// Backend-agnostic on purpose: it consumes whatever distance the active
// PositionHandler produced, so it keeps working if the GPS backend is
// selected for a comparison run.
//
// Not thread-safe. Only the main loop calls into it.
namespace track {

// Clear all history.
void init();

// Record a distance sample for `mac`.
void update(const uint8_t mac[6], float meters, uint32_t nowMs);

// Closing speed in m/s (positive means the gap is shrinking) and time to
// collision in seconds, or -1 when the pair is not converging fast enough for
// the number to mean anything.
//
// Returns false until there are at least two samples to draw a slope through.
bool query(const uint8_t mac[6], float& closingMps, float& ttcSeconds);

}  // namespace track
