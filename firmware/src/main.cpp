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

static const char* stateStr(MotionState s) {
  switch (s) {
    case BRAKING:      return "BRAKING";
    case ACCELERATING: return "ACCELERATING";
    default:           return "IDLE";
  }
}

void loop() {
  position.update();

  const int16_t local = motion::readSmoothedLocalAccel();
  comms::broadcastAccel(local);

  // Emit one NDJSON line per fresh peer, compatible with the monitor app.
  // Format: {"mac":"XX:XX:XX:XX:XX:XX","distance":12.3,"bearing":45.0,"state":"BRAKING"}
  {
    PeerState fresh[MAX_PEERS];
    const int n = peers::snapshotFresh(fresh, millis());
    for (int i = 0; i < n; i++) {
      float meters  = 0.0f;
      float bearing = 0.0f;
      bool hasDist    = position.distanceTo(fresh[i].mac, meters);
      bool hasBearing = position.bearingTo(fresh[i].mac, bearing);

      MotionState s = motion::classify(local, fresh[i].lastAccel);

      // Only emit if we have at least distance data.
      if (hasDist) {
        Serial.printf(
          "{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
          "\"distance\":%.1f,"
          "\"bearing\":%.1f,"
          "\"state\":\"%s\"}\n",
          fresh[i].mac[0], fresh[i].mac[1], fresh[i].mac[2],
          fresh[i].mac[3], fresh[i].mac[4], fresh[i].mac[5],
          meters,
          hasBearing ? bearing : 0.0f,
          stateStr(s));
      }

      // LED: light up if any peer is braking.
      if (s == BRAKING) {
        digitalWrite(LED_BUILTIN, HIGH);
      }
    }
  }

  // Turn LED off if nobody is braking (set above only if someone was).
  // Slight simplification: if at least one peer brakes, LED stays on
  // for this cycle; otherwise off.
  {
    MotionState worst = aggregateState(local);
    if (worst != BRAKING) {
      digitalWrite(LED_BUILTIN, LOW);
    }
  }

  delay(LOOP_INTERVAL_MS);
}
