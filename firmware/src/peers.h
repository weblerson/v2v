#pragma once

#include <stdint.h>
#include "config.h"

// Snapshot of one peer's most recent broadcast.
struct PeerState {
  uint8_t  mac[6];
  int16_t  lastAccel;
  uint32_t lastSeq;
  uint32_t lastRxMillis;
  bool     used;
};

namespace peers {

// Initialize the table (clears all slots).
void init();

// Insert or update the slot for `mac`. Rejects out-of-order/duplicate seq.
// Safe to call from the ESP-NOW RX callback. Returns true if accepted.
bool upsert(const uint8_t mac[6], uint32_t seq, int16_t accel, uint32_t nowMs);

// Copy a snapshot of all currently-fresh peers (lastRxMillis within
// REMOTE_TIMEOUT_MS of `nowMs`) into `out`. Returns the count written.
// `out` must hold at least MAX_PEERS entries. Safe vs. the RX callback.
int snapshotFresh(PeerState* out, uint32_t nowMs);

}  // namespace peers
