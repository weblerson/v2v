#include "comms.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <string.h>

#include "protocol.h"
#include "peers.h"

namespace {

uint8_t                   selfMac[6]         = {0};
uint8_t                   broadcastAddr[6]   = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint32_t                  txSeq              = 0;
comms::PositionRxHandler  positionRxHandler  = nullptr;

void onDataSent(const uint8_t* /*mac_addr*/, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("ESP-NOW send failed");
  }
}

void onDataRecv(const uint8_t* srcMac, const uint8_t* data, int len) {
  // Dispatch by payload size. Every packet type carries its own `mac`
  // field, which we cross-check against the link-layer source for anti-
  // spoofing, and we always drop our own broadcasts (we're a member of
  // the FF:FF:FF:FF:FF:FF peer group too).
  if (len == (int)sizeof(V2VPacket)) {
    V2VPacket pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (memcmp(pkt.mac, srcMac, 6) != 0) return;
    if (memcmp(pkt.mac, selfMac, 6) == 0) return;
    peers::upsert(pkt.mac, pkt.seq, pkt.accel, millis());
    return;
  }

  if (len == (int)sizeof(PositionPacket)) {
    PositionPacket pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (memcmp(pkt.mac, srcMac, 6) != 0) return;
    if (memcmp(pkt.mac, selfMac, 6) == 0) return;
    if (positionRxHandler) positionRxHandler(srcMac, pkt);
    return;
  }
  // Unknown length — silently drop.
}

}  // namespace

namespace comms {

bool begin() {
  WiFi.mode(WIFI_STA);
  WiFi.macAddress(selfMac);
  Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
    selfMac[0], selfMac[1], selfMac[2], selfMac[3], selfMac[4], selfMac[5]);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return false;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddr, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add broadcast peer");
    return false;
  }

  return true;
}

void broadcastAccel(int16_t accel) {
  V2VPacket pkt;
  memcpy(pkt.mac, selfMac, 6);
  pkt.seq   = ++txSeq;
  pkt.accel = accel;
  esp_now_send(broadcastAddr, (const uint8_t*)&pkt, sizeof(pkt));
}

bool broadcastRaw(const void* data, size_t len) {
  return esp_now_send(broadcastAddr, (const uint8_t*)data, len) == ESP_OK;
}

void setPositionRxHandler(PositionRxHandler handler) {
  positionRxHandler = handler;
}

const uint8_t* localMac() { return selfMac; }

}  // namespace comms
