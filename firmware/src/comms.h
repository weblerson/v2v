#pragma once

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

namespace comms {

// Signature for modules that want to be notified of incoming
// PositionPackets. `srcMac` is the ESP-NOW link-layer source (already
// verified to match the payload mac field).
using PositionRxHandler = void(*)(const uint8_t srcMac[6], const PositionPacket& pkt);

// Bring up WiFi (STA), ESP-NOW, register the broadcast peer and callbacks.
// Caches the local MAC for use in outgoing packets and self-filtering.
// Returns false on any failure.
bool begin();

// Broadcast the current local accel to all peers on the channel.
// Increments and embeds an internal monotonic seq.
void broadcastAccel(int16_t accel);

// Generic broadcast of an arbitrary payload (used by the positioning module
// to emit PositionPackets). Returns the esp_now_send result == ESP_OK.
bool broadcastRaw(const void* data, size_t len);

// Register a handler to be invoked from the ESP-NOW RX callback whenever a
// well-formed PositionPacket arrives. Pass nullptr to unregister. Only one
// handler is stored — last call wins.
void setPositionRxHandler(PositionRxHandler handler);

// The cached local MAC (6 bytes). Valid after begin().
const uint8_t* localMac();

}  // namespace comms
