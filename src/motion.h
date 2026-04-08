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
// peer's accel, applying hysteresis against the previous state.
MotionState classify(int16_t localAccel, int16_t remoteAccel);

}  // namespace motion
