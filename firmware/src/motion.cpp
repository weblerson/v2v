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
#if DEBUG
  static uint32_t lastMpuReport = 0;
  uint32_t now = millis();
  if (now - lastMpuReport >= 3000) {
    lastMpuReport = now;
    Serial.printf("[MPU] ax=%d ay=%d az=%d\n", ax, ay, az);
  }
#endif
  return pushSample((int16_t)(ax - baselineX));
}

MotionState classify(int16_t localAccel, int16_t remoteAccel, MotionState prev) {
  const int32_t rel    = (int32_t)remoteAccel - (int32_t)localAccel;
  const int32_t enterT = ACCEL_THRESHOLD;
  const int32_t exitT  = (int32_t)(ACCEL_THRESHOLD * HYSTERESIS_EXIT_RATIO);

  MotionState next = prev;
  switch (prev) {
    case BRAKING:
      if (rel > -exitT) next = (rel > enterT) ? ACCELERATING : IDLE;
      break;
    case ACCELERATING:
      if (rel < exitT) next = (rel < -enterT) ? BRAKING : IDLE;
      break;
    case IDLE:
    default:
      if (rel < -enterT)      next = BRAKING;
      else if (rel > enterT)  next = ACCELERATING;
      break;
  }
  return next;
}

}  // namespace motion
