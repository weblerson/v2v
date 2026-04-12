#include <Arduino.h>

#include "config.h"
#include "motion.h"
#include "comms.h"
#include "peers.h"
#include "position.h"
#include "position_gps.h"

// Active positioning backend. Swap to PositionDWMHandler when the UWB
// hardware lands — no other file needs to change.
static PositionGPSHandler gpsHandler;
static PositionHandler&   position = gpsHandler;

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

  if (!position.begin()) {
    Serial.println("Position handler init failed — continuing without positioning");
  }

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
  position.update();

  const int16_t local = motion::readSmoothedLocalAccel();
  comms::broadcastAccel(local);

  // Log distance to every currently-fresh peer, when positioning has data.
  {
    PeerState fresh[MAX_PEERS];
    const int n = peers::snapshotFresh(fresh, millis());
    for (int i = 0; i < n; i++) {
      float meters = 0.0f;
      if (position.distanceTo(fresh[i].mac, meters)) {
        Serial.printf("Peer %02X:%02X:%02X:%02X:%02X:%02X  distance=%.1f m\n",
          fresh[i].mac[0], fresh[i].mac[1], fresh[i].mac[2],
          fresh[i].mac[3], fresh[i].mac[4], fresh[i].mac[5], meters);
      }
    }
  }

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
