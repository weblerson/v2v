#pragma once

#include <stdint.h>

#include "config.h"

// Two-Way Ranging over a Decawave DWM1000, on top of the vendored DW1000Ng
// driver (see firmware/lib/DW1000Ng/VENDORING.md).
//
// Design notes — the reasoning behind these choices lives in
// docs/PLAN_DWM1000.md (decisions D2 through D6):
//
//   * Asymmetric double-sided TWR, four frames. Single-sided ranging is
//     dominated by the clock offset between the two crystals (20 ppm over a
//     400 us reply is ~2.4 m of error); DS-TWR cancels that to first order.
//   * Peers are discovered over ESP-NOW, not over UWB. The peer table in
//     peers.h is the source of truth for "who is out there"; this module only
//     answers "how far".
//   * For each pair, the node with the lexicographically smaller MAC is the
//     initiator and the other is the responder. That removes simultaneous-
//     initiation collisions by construction, and since the exchange ends with
//     a RANGE_REPORT, both sides learn the distance anyway.
//   * Everything runs in a dedicated FreeRTOS task. A full exchange takes
//     ~8 ms, which must not sit inside the 100 ms main loop.
namespace uwb {

// Bring up the radio and start the ranging task.
//
// Returns false when the DW1000 does not answer over SPI — wrong wiring, no
// power, or a dead module. Callers should treat that as "no distance data
// available" rather than a fatal error.
bool begin();

// Latest range to `mac`, if a successful exchange happened within
// UWB_RANGE_MAX_AGE_MS of `nowMs`. Never returns a stale value: in a moving
// vehicle a cached distance is worse than no distance.
bool rangeTo(const uint8_t mac[6], uint32_t nowMs, float& meters);

// Latest range regardless of age, plus when it was measured. For diagnostics
// and calibration, where "is this a new sample?" matters more than freshness.
bool lastRange(const uint8_t mac[6], float& meters, uint32_t& measuredAtMs);

// Counters for the metrics campaign (METRICS.md).
uint32_t exchangesOk();
uint32_t exchangesFailed();

// Antenna delay currently in effect, in DW1000 ticks.
uint16_t antennaDelay();

// Override the antenna delay, optionally persisting it to NVS so it survives
// a reboot. Used by the `uwb_calib` environment; ~213 ticks is one metre of
// constant range bias, which is why this has to be measured per board.
void setAntennaDelay(uint16_t ticks, bool persist);

// Erase the stored calibration, reverting to UWB_ANTENNA_DELAY_DEFAULT on the
// next boot.
void clearStoredAntennaDelay();

}  // namespace uwb
