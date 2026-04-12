#pragma once

#include "position.h"
#include "protocol.h"
#include "config.h"

// GPS-based positioning using a NEO-6M connected to Serial2.
//
// Each instance:
//   * drains NMEA from Serial2 on every update()
//   * when a fresh fix is available, broadcasts its lat/lon as a
//     PositionPacket through the comms layer
//   * maintains a MAC-keyed table of peer positions populated from
//     incoming PositionPackets (routed by comms::setPositionRxHandler)
//   * computes distance via Haversine on demand in distanceTo()
//
// Freshness is enforced by POSITION_MAX_AGE_MS on BOTH sides (local and
// peer) — stale data is never returned.
class PositionGPSHandler : public PositionHandler {
 public:
  PositionGPSHandler();

  bool begin() override;
  void update() override;
  bool distanceTo(const uint8_t mac[6], float& meters) override;

 private:
  struct PeerPosition {
    uint8_t  mac[6];
    double   lat;
    double   lon;
    uint32_t lastRxMillis;
    bool     used;
  };

  // Trampoline registered with comms — forwards to the singleton instance.
  static void onPositionRx(const uint8_t srcMac[6], const PositionPacket& pkt);
  void ingestPeerPosition(const uint8_t mac[6], double lat, double lon, uint32_t nowMs);

  PeerPosition peers_[MAX_PEERS];

  double   localLat_         = 0.0;
  double   localLon_         = 0.0;
  uint32_t localFixMillis_   = 0;
  bool     hasLocalFix_      = false;

  uint32_t txSeq_            = 0;
  uint32_t lastBroadcastMs_  = 0;
};
