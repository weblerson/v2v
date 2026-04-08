#include <Arduino.h>

#include "config.h"
#include "motion.h"
#include "comms.h"
#include "peers.h"

void setup() {
  Serial.begin(115200);
  delay(100);

  if (!motion::begin()) {
    Serial.println("MPU6050 connection failed — halting");
    while (true) delay(1000);
  }
  Serial.println("MPU6050 connected");

  motion::calibrate();
  peers::init();

  if (!comms::begin()) {
    Serial.println("Comms init failed — halting");
    while (true) delay(1000);
  }
  Serial.println("Comms ready");

  pinMode(LED_BUILTIN, OUTPUT);
}

// Aggregate the worst state across all currently-fresh peers.
// BRAKING > ACCELERATING > IDLE.
static MotionState aggregateState(int16_t localAccel) {
  PeerState fresh[MAX_PEERS];
  const int n = peers::snapshotFresh(fresh, millis());

  MotionState worst = IDLE;
  for (int i = 0; i < n; i++) {
    MotionState s = motion::classify(localAccel, fresh[i].lastAccel);
    if (s == BRAKING) return BRAKING;
    if (s == ACCELERATING) worst = ACCELERATING;
  }
  return worst;
}

void loop() {
  const int16_t local = motion::readSmoothedLocalAccel();
  comms::broadcastAccel(local);

  switch (aggregateState(local)) {
    case BRAKING:
      Serial.println("Relative: BRAKING");
      digitalWrite(LED_BUILTIN, HIGH);
      break;
    case ACCELERATING:
      Serial.println("Relative: ACCELERATING");
      digitalWrite(LED_BUILTIN, LOW);
      break;
    case IDLE:
    default:
      digitalWrite(LED_BUILTIN, LOW);
      break;
  }

  delay(LOOP_INTERVAL_MS);
}
