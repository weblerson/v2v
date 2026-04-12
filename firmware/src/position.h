#pragma once

#include <stdint.h>

// Abstract positioning interface. The intent is that the V2V logic only
// ever asks "how far am I from peer X?" — it never sees absolute
// coordinates, ranging protocols, or radio-specific details. Swap the
// implementation (GPS now, DWM1000 later) without touching callers.
class PositionHandler {
 public:
  virtual ~PositionHandler() = default;

  // One-time bring-up: sensor init, buffer allocation, registering any
  // callbacks with the comms layer. Returns false on hardware failure.
  virtual bool begin() = 0;

  // Called from loop(). Must be non-blocking. Responsible for draining
  // sensor I/O, decoding frames, broadcasting local position as needed,
  // and aging out stale peer data.
  virtual void update() = 0;

  // Resolve the current distance in meters from this device to the peer
  // identified by `mac`. Returns true ONLY when both the local fix AND
  // the peer's position are fresher than POSITION_MAX_AGE_MS.
  //
  // A stale value is never returned: in a moving vehicle, a cached
  // "safe distance" from seconds ago is actively dangerous.
  virtual bool distanceTo(const uint8_t mac[6], float& meters) = 0;
};
