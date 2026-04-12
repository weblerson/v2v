#pragma once

#include "position.h"

// Positioning via a Decawave DWM1000/DWM3000 UWB module over SPI, using
// Two-Way Ranging (TWR) to measure direct peer-to-peer distance at
// ~10 cm accuracy.
//
// TODO: implement once the hardware arrives. Sketch of the plan:
//   * Wire DWM1000 to VSPI (MOSI/MISO/SCK + CS + IRQ + RST).
//   * Use the `arduino-dw1000` library or Decawave's SDK.
//   * begin():       configure radio (channel, PRF, data rate), register
//                    IRQ handler, start listening.
//   * update():      drive the TWR state machine — initiate a ranging
//                    exchange round-robin against known peers (peer MACs
//                    come from the existing comms peer table) and store
//                    the resulting distance per peer with a timestamp.
//   * distanceTo():  same freshness contract as the GPS handler — only
//                    return a value if the last successful ranging for
//                    that MAC is within POSITION_MAX_AGE_MS.
//
// Unlike the GPS handler, this implementation must NOT broadcast any
// PositionPacket — ranging happens entirely on the UWB radio. When this
// class is active, the PositionPacket type defined in protocol.h becomes
// dead weight and can be removed (see the TODO note there).
class PositionDWMHandler : public PositionHandler {
 public:
  PositionDWMHandler() = default;

  bool begin() override;
  void update() override;
  bool distanceTo(const uint8_t mac[6], float& meters) override;
  bool bearingTo(const uint8_t mac[6], float& degrees) override;
};
