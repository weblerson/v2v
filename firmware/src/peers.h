#pragma once

#include <stdint.h>
#include "config.h"
#include "motion.h"

// Snapshot of one peer's most recent broadcast.
struct PeerState {
  uint8_t     mac[6];
  int16_t     lastAccel;
  uint32_t    lastSeq;
  uint32_t    lastRxMillis;
  // Hysteresis state for this peer's motion classification. Lives here, not
  // in motion.cpp, so that one peer's history cannot leak into another's.
  MotionState lastMotion;
  bool        used;
};

namespace peers {

// Initialize the table (clears all slots).
void init();

// Insert or update the slot for `mac`. Rejects out-of-order/duplicate seq,
// while still re-admitting a peer that rebooted and restarted its counter.
// Safe to call from the ESP-NOW RX callback. Returns true if accepted.
bool upsert(const uint8_t mac[6], uint32_t seq, int16_t accel, uint32_t nowMs);

// Store the motion state decided for `mac` this cycle, so the next cycle's
// hysteresis has the right starting point. No-op for an unknown MAC.
void updateMotion(const uint8_t mac[6], MotionState state);

// Copy a snapshot of all currently-fresh peers (lastRxMillis within
// REMOTE_TIMEOUT_MS of `nowMs`) into `out`. Returns the count written.
// `out` must hold at least MAX_PEERS entries. Safe vs. the RX callback.
int snapshotFresh(PeerState* out, uint32_t nowMs);

}  // namespace peers
