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
