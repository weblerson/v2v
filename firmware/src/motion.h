#pragma once

#include <stdint.h>

enum MotionState { IDLE, ACCELERATING, BRAKING };

namespace motion {

// Initialize MPU6050 + I2C. Returns false on connection failure.
bool begin();

// Sample the device at rest and store the X-axis baseline.
void calibrate();

// Read the current forward acceleration (X, baseline-corrected) and feed it
// through the moving-average filter. Returns the smoothed value.
int16_t readSmoothedLocalAccel();

// Decide the relative motion state given the latest local accel and one
// peer's accel, applying hysteresis against `prev` — that peer's own previous
// state.
//
// Pure function: the caller owns the state, one value per peer (PeerState in
// peers.h). It used to hold a single shared static instead, which let one
// peer's hysteresis decide another peer's classification, and made the result
// depend on how many times per cycle the function happened to be called.
MotionState classify(int16_t localAccel, int16_t remoteAccel, MotionState prev);

}  // namespace motion
