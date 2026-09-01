#include "position_dwm.h"

#include <Arduino.h>

#include "uwb_twr.h"

bool PositionDWMHandler::begin() {
  ready_ = uwb::begin();
  return ready_;
}

void PositionDWMHandler::update() {
  // Deliberately empty. Ranging runs in the task started by uwb::begin();
  // there is nothing to drain or age here, and blocking this call would stall
  // the main loop for the duration of an exchange.
}

bool PositionDWMHandler::distanceTo(const uint8_t mac[6], float& meters) {
  if (!ready_) return false;
  return uwb::rangeTo(mac, millis(), meters);
}

bool PositionDWMHandler::bearingTo(const uint8_t /*mac*/[6], float& /*degrees*/) {
  // No angle from a single UWB antenna — see the header for why this is a
  // hardware limit and not a missing feature.
  return false;
}
