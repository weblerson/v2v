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
  int idx = findSlot(mac);
  if (idx < 0) {
    idx = allocSlot(nowMs);
    memcpy(table[idx].mac, mac, 6);
    table[idx].lastSeq = 0;
    table[idx].used = true;
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
