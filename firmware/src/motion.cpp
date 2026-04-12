#include "motion.h"

#include <Arduino.h>
#include <Wire.h>
#include <MPU6050.h>

#include "config.h"

namespace {

MPU6050  mpu;
int16_t  baselineX = 0;

// Ring buffer for the moving average over local accel readings.
int16_t  window[SMOOTHING_WINDOW] = {0};
int      windowIdx   = 0;
int      windowCount = 0;
long     windowSum   = 0;

// Hysteresis: previous decision per peer is held by the caller, but the
// classifier is stateless across peers — we use a static last-state per
// caller via a small trick: classify() takes only data, and the caller
// stores the result. Here we expose a helper that applies hysteresis given
// a "current state" hint passed back in. To keep the API simple we keep one
// shared previous state — fine for the common 1-peer case; main.cpp can be
// extended later to track per-peer history if needed.
MotionState lastState = IDLE;

int16_t pushSample(int16_t sample) {
  windowSum -= window[windowIdx];
  window[windowIdx] = sample;
  windowSum += sample;
  windowIdx = (windowIdx + 1) % SMOOTHING_WINDOW;
  if (windowCount < SMOOTHING_WINDOW) windowCount++;
  return (int16_t)(windowSum / windowCount);
}

}  // namespace

namespace motion {

bool begin() {
  Wire.begin();
  mpu.initialize();
  return mpu.testConnection();
}

void calibrate() {
  long sum = 0;
  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    sum += ax;
    delay(10);
  }
  baselineX = sum / CALIBRATION_SAMPLES;
  Serial.printf("MPU6050 calibrated. Baseline X: %d\n", baselineX);
}

int16_t readSmoothedLocalAccel() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  return pushSample((int16_t)(ax - baselineX));
}

MotionState classify(int16_t localAccel, int16_t remoteAccel) {
  const int32_t rel    = (int32_t)remoteAccel - (int32_t)localAccel;
  const int32_t enterT = ACCEL_THRESHOLD;
  const int32_t exitT  = (int32_t)(ACCEL_THRESHOLD * HYSTERESIS_EXIT_RATIO);

  switch (lastState) {
    case BRAKING:
      if (rel > -exitT) lastState = (rel > enterT) ? ACCELERATING : IDLE;
      break;
    case ACCELERATING:
      if (rel < exitT) lastState = (rel < -enterT) ? BRAKING : IDLE;
      break;
    case IDLE:
    default:
      if (rel < -enterT)      lastState = BRAKING;
      else if (rel > enterT)  lastState = ACCELERATING;
      break;
  }
  return lastState;
}

}  // namespace motion
