#include "peers.h"

#include <Arduino.h>
#include <string.h>

namespace {

PeerState        table[MAX_PEERS];
portMUX_TYPE     mux = portMUX_INITIALIZER_UNLOCKED;

bool macEqual(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

// Find slot index for `mac`, or -1.
int findSlot(const uint8_t mac[6]) {
  for (int i = 0; i < MAX_PEERS; i++) {
    if (table[i].used && macEqual(table[i].mac, mac)) return i;
  }
  return -1;
}

// Pick a slot for a new peer: first free, otherwise evict the most stale.
int allocSlot(uint32_t nowMs) {
  int  oldestIdx = 0;
  uint32_t oldestAge = 0;
  for (int i = 0; i < MAX_PEERS; i++) {
    if (!table[i].used) return i;
    uint32_t age = nowMs - table[i].lastRxMillis;
    if (age >= oldestAge) {
      oldestAge = age;
      oldestIdx = i;
    }
  }
  return oldestIdx;
}

}  // namespace

namespace peers {

void init() {
  portENTER_CRITICAL(&mux);
  memset(table, 0, sizeof(table));
  portEXIT_CRITICAL(&mux);
}

bool upsert(const uint8_t mac[6], uint32_t seq, int16_t accel, uint32_t nowMs) {
  bool accepted = false;
  portENTER_CRITICAL(&mux);
  int  idx     = findSlot(mac);
  bool isNew   = false;
  if (idx < 0) {
    idx = allocSlot(nowMs);
    memset(&table[idx], 0, sizeof(PeerState));
    memcpy(table[idx].mac, mac, 6);
    table[idx].used = true;
    isNew = true;
  }

  // A peer that reboots starts counting from 1 again. The monotonic rule below
  // would then reject every packet it ever sends, and the peer would vanish
  // from the radar until *this* device restarts — a silent failure in a safety
  // system. Two independent signals mark a new run rather than a late packet:
  // we have heard nothing for longer than the staleness window, or the counter
  // jumped far backwards. Inactivity alone covers the real case (an ESP32 takes
  // well over REMOTE_TIMEOUT_MS to boot); the rollback check is the backstop.
  const bool wasSilent =
      (uint32_t)(nowMs - table[idx].lastRxMillis) > REMOTE_TIMEOUT_MS;
  const bool bigRollback =
      seq < table[idx].lastSeq && (table[idx].lastSeq - seq) > SEQ_REBOOT_GAP;
  if (isNew || wasSilent || bigRollback) {
    table[idx].lastSeq    = 0;
    table[idx].lastMotion = IDLE;
  }

  // Reject duplicates / out-of-order. Allow seq==0 for fresh slots.
  if (seq > table[idx].lastSeq || table[idx].lastSeq == 0) {
    table[idx].lastSeq      = seq;
    table[idx].lastAccel    = accel;
    table[idx].lastRxMillis = nowMs;
    accepted = true;
  }
  portEXIT_CRITICAL(&mux);
  return accepted;
}

void updateMotion(const uint8_t mac[6], MotionState state) {
  portENTER_CRITICAL(&mux);
  const int idx = findSlot(mac);
  if (idx >= 0) table[idx].lastMotion = state;
  portEXIT_CRITICAL(&mux);
}

int snapshotFresh(PeerState* out, uint32_t nowMs) {
  int n = 0;
  portENTER_CRITICAL(&mux);
  for (int i = 0; i < MAX_PEERS; i++) {
    if (!table[i].used) continue;
    if (nowMs - table[i].lastRxMillis > REMOTE_TIMEOUT_MS) continue;
    out[n++] = table[i];
  }
  portEXIT_CRITICAL(&mux);
  return n;
}

}  // namespace peers
