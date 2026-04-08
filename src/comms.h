#pragma once

#include <stdint.h>

namespace comms {

// Bring up WiFi (STA), ESP-NOW, register the broadcast peer and callbacks.
// Caches the local MAC for use in outgoing packets and self-filtering.
// Returns false on any failure.
bool begin();

// Broadcast the current local accel to all peers on the channel.
// Increments and embeds an internal monotonic seq.
void broadcastAccel(int16_t accel);

// The cached local MAC (6 bytes). Valid after begin().
const uint8_t* localMac();

}  // namespace comms
