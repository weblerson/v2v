#include <Arduino.h>

#include "config.h"
#include "motion.h"
#include "comms.h"
#include "peers.h"
#include "position.h"
#include "position_dwm.h"
#include "track.h"

// Active positioning backend. UWB replaced GPS outright rather than
// supplementing it (docs/PLAN_DWM1000.md, D10). PositionGPSHandler is still in
// the tree and swapping back is a one-line change here — that is how the
// metrics campaign compares both backends over the same runs.
static PositionDWMHandler dwmHandler;
static PositionHandler&   position = dwmHandler;

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
  track::init();

  if (!comms::begin()) {
    Serial.println("Comms init failed — halting");
    while (true) delay(1000);
  }
  Serial.println("Comms ready");

  // A dead or unwired UWB module is not fatal: peers still arrive over
  // ESP-NOW and braking alerts still fire. Only distance goes missing, and the
  // loop below already declines to report a peer it cannot range.
  if (!position.begin()) {
    Serial.println("Position handler init failed — continuing without distance");
  }

  pinMode(LED_BUILTIN, OUTPUT);
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

  const uint32_t nowMs = millis();
  PeerState      fresh[MAX_PEERS];
  const int      n = peers::snapshotFresh(fresh, nowMs);

  bool anyBraking = false;

  for (int i = 0; i < n; i++) {
    // Exactly one classification per peer per cycle, seeded with that peer's
    // own previous state and written straight back. Classifying twice would
    // advance the hysteresis machine twice for a single set of readings.
    const MotionState state =
        motion::classify(local, fresh[i].lastAccel, fresh[i].lastMotion);
    peers::updateMotion(fresh[i].mac, state);
    if (state == BRAKING) anyBraking = true;

    // No distance means nothing worth drawing on the radar. The peer is still
    // counted above for the braking alert.
    float meters = 0.0f;
    if (!position.distanceTo(fresh[i].mac, meters)) continue;

    track::update(fresh[i].mac, meters, nowMs);
    float closing = 0.0f;
    float ttc     = -1.0f;
    track::query(fresh[i].mac, closing, ttc);

    // Always false on the UWB backend — a single antenna measures time of
    // flight, not direction. Reported explicitly so the monitor can draw a
    // ring at the measured radius instead of a marker in a made-up direction.
    float      bearing    = 0.0f;
    const bool hasBearing = position.bearingTo(fresh[i].mac, bearing);

    // NDJSON, one object per line. `ttc` is -1 when the pair is not closing
    // fast enough for a time-to-collision to mean anything.
    Serial.printf(
        "{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
        "\"distance\":%.2f,"
        "\"bearing\":%.1f,"
        "\"bearing_valid\":%s,"
        "\"closing\":%.2f,"
        "\"ttc\":%.1f,"
        "\"state\":\"%s\"}\n",
        fresh[i].mac[0], fresh[i].mac[1], fresh[i].mac[2],
        fresh[i].mac[3], fresh[i].mac[4], fresh[i].mac[5],
        meters,
        hasBearing ? bearing : 0.0f,
        hasBearing ? "true" : "false",
        closing,
        ttc,
        stateStr(state));
  }

  digitalWrite(LED_BUILTIN, anyBraking ? HIGH : LOW);

  delay(LOOP_INTERVAL_MS);
}
