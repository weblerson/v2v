#include "position_gps.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include <math.h>
#include <string.h>

#include "comms.h"

namespace {

// NEO-6M lives on Serial2. One static instance is enough — there is only
// one GPS antenna per car.
HardwareSerial gpsSerial(2);
TinyGPSPlus    gps;

// Active singleton pointer used by the C-style RX trampoline. Set in
// begin(), cleared never (handler lives for the program lifetime).
PositionGPSHandler* activeInstance = nullptr;

// Guards peer position table against concurrent writes from the ESP-NOW
// RX callback context.
portMUX_TYPE peerMux = portMUX_INITIALIZER_UNLOCKED;

constexpr double EARTH_RADIUS_M = 6371000.0;

double deg2rad(double d) { return d * (M_PI / 180.0); }

// Great-circle distance between two WGS84 points, in meters.
double haversine(double lat1, double lon1, double lat2, double lon2) {
  const double dLat = deg2rad(lat2 - lat1);
  const double dLon = deg2rad(lon2 - lon1);
  const double a = sin(dLat / 2) * sin(dLat / 2) +
                   cos(deg2rad(lat1)) * cos(deg2rad(lat2)) *
                   sin(dLon / 2) * sin(dLon / 2);
  const double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return EARTH_RADIUS_M * c;
}

bool macEqual(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

}  // namespace

PositionGPSHandler::PositionGPSHandler() {
  memset(peers_, 0, sizeof(peers_));
}

bool PositionGPSHandler::begin() {
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  activeInstance = this;
  comms::setPositionRxHandler(&PositionGPSHandler::onPositionRx);

  Serial.printf("GPS on Serial2 @ %lu baud (RX=%d TX=%d)\n",
                (unsigned long)GPS_BAUD, GPS_RX_PIN, GPS_TX_PIN);
  return true;
}

void PositionGPSHandler::update() {
  // Drain whatever NMEA bytes have accumulated since the last call.
  // TinyGPSPlus is purely incremental — this never blocks.
  while (gpsSerial.available() > 0) {
    gps.encode((char)gpsSerial.read());
  }

  const uint32_t nowMs = millis();

  if (gps.location.isValid() && gps.location.isUpdated()) {
    localLat_       = gps.location.lat();
    localLon_       = gps.location.lng();
    localFixMillis_ = nowMs;
    hasLocalFix_    = true;

    // Broadcast our new fix. Rate-limit to at most once per 200 ms so a
    // chatty GPS module can't flood the ESP-NOW channel.
    if (nowMs - lastBroadcastMs_ >= 200) {
      PositionPacket pkt;
      memcpy(pkt.mac, comms::localMac(), 6);
      pkt.seq = ++txSeq_;
      pkt.lat = localLat_;
      pkt.lon = localLon_;
      comms::broadcastRaw(&pkt, sizeof(pkt));
      lastBroadcastMs_ = nowMs;
    }
  }
}

bool PositionGPSHandler::distanceTo(const uint8_t mac[6], float& meters) {
  const uint32_t nowMs = millis();

  // Local fix must be fresh — no fallback to cached values.
  if (!hasLocalFix_ || (nowMs - localFixMillis_) > POSITION_MAX_AGE_MS) {
    return false;
  }

  double peerLat = 0, peerLon = 0;
  bool   peerFresh = false;

  portENTER_CRITICAL(&peerMux);
  for (int i = 0; i < MAX_PEERS; i++) {
    if (!peers_[i].used) continue;
    if (!macEqual(peers_[i].mac, mac)) continue;
    if ((nowMs - peers_[i].lastRxMillis) > POSITION_MAX_AGE_MS) break;
    peerLat   = peers_[i].lat;
    peerLon   = peers_[i].lon;
    peerFresh = true;
    break;
  }
  portEXIT_CRITICAL(&peerMux);

  if (!peerFresh) return false;

  meters = (float)haversine(localLat_, localLon_, peerLat, peerLon);
  return true;
}

void PositionGPSHandler::ingestPeerPosition(const uint8_t mac[6], double lat, double lon, uint32_t nowMs) {
  portENTER_CRITICAL(&peerMux);

  // Update existing slot if we know this MAC.
  int target = -1;
  for (int i = 0; i < MAX_PEERS; i++) {
    if (peers_[i].used && macEqual(peers_[i].mac, mac)) { target = i; break; }
  }
  // Otherwise take a free slot, or evict the most stale one.
  if (target < 0) {
    uint32_t oldestAge = 0;
    int      oldestIdx = 0;
    for (int i = 0; i < MAX_PEERS; i++) {
      if (!peers_[i].used) { target = i; break; }
      const uint32_t age = nowMs - peers_[i].lastRxMillis;
      if (age >= oldestAge) { oldestAge = age; oldestIdx = i; }
    }
    if (target < 0) target = oldestIdx;
    memcpy(peers_[target].mac, mac, 6);
    peers_[target].used = true;
  }

  peers_[target].lat          = lat;
  peers_[target].lon          = lon;
  peers_[target].lastRxMillis = nowMs;

  portEXIT_CRITICAL(&peerMux);
}

// static
void PositionGPSHandler::onPositionRx(const uint8_t srcMac[6], const PositionPacket& pkt) {
  if (!activeInstance) return;
  activeInstance->ingestPeerPosition(srcMac, pkt.lat, pkt.lon, millis());
}
