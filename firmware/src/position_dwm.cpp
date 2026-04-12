#include "position_dwm.h"

// TODO(DWM): full implementation pending hardware arrival. See header for
// the intended design. All methods are deliberate no-ops so the class can
// be instantiated for compile-time interface checks without misbehaving
// at runtime (distanceTo never reports a fresh value → callers safely
// treat every peer as "unknown distance").

bool PositionDWMHandler::begin() {
  return false;  // not implemented yet
}

void PositionDWMHandler::update() {
  // no-op
}

bool PositionDWMHandler::distanceTo(const uint8_t /*mac*/[6], float& /*meters*/) {
  return false;
}

bool PositionDWMHandler::bearingTo(const uint8_t /*mac*/[6], float& /*degrees*/) {
  return false;
}
