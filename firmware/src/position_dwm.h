#pragma once

#include "position.h"

// Positioning via a Decawave DWM1000 UWB module over SPI, using asymmetric
// double-sided Two-Way Ranging to measure peer-to-peer distance directly.
//
// This is the active backend: UWB replaced GPS rather than supplementing it
// (docs/PLAN_DWM1000.md, D10). Almost nothing happens in this class — the
// ranging state machine lives in uwb_twr.cpp and runs in its own FreeRTOS
// task, because one exchange takes ~8 ms and update() is contractually
// non-blocking.
//
// bearingTo() always fails, and that is not an omission. The DWM1000 measures
// time of flight, not direction; angle of arrival needs phase difference
// across two antennas, which is a DW3000 feature. Rather than publishing a
// plausible-looking zero — which would draw every vehicle straight ahead on
// the radar — the unknown is reported honestly and the monitor draws a ring at
// the measured radius. See D8 for the alternatives that were weighed, and why
// deriving the angle from two GPS fixes was rejected: at 3 m of separation the
// resulting bearing error is roughly ±30°, which is worst exactly inside the
// red zone where the alert matters.
class PositionDWMHandler : public PositionHandler {
 public:
  PositionDWMHandler() = default;

  bool begin() override;
  void update() override;
  bool distanceTo(const uint8_t mac[6], float& meters) override;
  bool bearingTo(const uint8_t mac[6], float& degrees) override;

 private:
  bool ready_ = false;
};
