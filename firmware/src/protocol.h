#pragma once

#include <stdint.h>

// Wire format broadcast over ESP-NOW. Kept small and fixed-size so the
// receiver can validate by length alone.
//
// `mac` is the sender's own MAC, duplicated in the payload so the receiver
// can cross-check it against esp_now_recv_info_t.src_addr (cheap anti-spoof).
struct __attribute__((packed)) V2VPacket {
  uint8_t  mac[6];
  uint32_t seq;
  int16_t  accel;  // baseline-corrected forward acceleration (raw LSB)
};

// Broadcast by the GPS positioning handler. Each car emits its own fix so
// peers can compute relative distance via Haversine.
//
// No longer on the production path: positioning moved to Two-Way Ranging over
// UWB, which happens entirely on the DWM1000's own radio and needs nothing
// from ESP-NOW (docs/PLAN_DWM1000.md, D10). This type — along with
// comms::broadcastRaw() and comms::setPositionRxHandler() — now exists solely
// for PositionGPSHandler, which is kept compiling so the metrics campaign can
// measure both backends over the same runs.
struct __attribute__((packed)) PositionPacket {
  uint8_t  mac[6];
  uint32_t seq;
  double   lat;   // decimal degrees, WGS84
  double   lon;   // decimal degrees, WGS84
};
