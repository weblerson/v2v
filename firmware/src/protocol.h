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
// TODO(DWM): when the DWM1000/DWM3000 arrives, positioning will switch to
// Two-Way Ranging over UWB. At that point this packet type becomes unused
// — ranging happens on the UWB radio, not ESP-NOW — and should be removed
// (or gated behind a compile-time flag if we want to keep GPS as a
// fallback for long-range scenarios).
struct __attribute__((packed)) PositionPacket {
  uint8_t  mac[6];
  uint32_t seq;
  double   lat;   // decimal degrees, WGS84
  double   lon;   // decimal degrees, WGS84
};
