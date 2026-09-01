// Bring-up stage E1: is the DWM1000 alive and wired correctly?
//
//   pio run -e uwb_probe -t upload && pio device monitor -e uwb_probe
//
// Reads the DW1000's DEV_ID register over raw SPI, deliberately without going
// through the driver library, so a failure here can only mean wiring, power or
// a dead module — never our code. A healthy DW1000 answers 0xDECA0130.
//
// Run this on each board, one at a time, BEFORE wiring the second module: if
// something is wrong with the harness, discovering it with one module at risk
// is much better than with two. See docs/PLAN_DWM1000.md §7 for the electrical
// checklist that goes with this.

#include <Arduino.h>
#include <SPI.h>

#include "config.h"

namespace {

constexpr uint32_t DW1000_DEV_ID = 0xDECA0130UL;

// The datasheet caps SPI at 3 MHz until the chip's PLL has locked (Table 2),
// so the probe stays well under that.
constexpr uint32_t PROBE_SPI_HZ = 2000000;

void hardReset() {
  // RSTn must never be driven high by an external source (datasheet p.5 and
  // p.17). Pull it low, then hand it back to high impedance.
  pinMode(UWB_PIN_RST, OUTPUT);
  digitalWrite(UWB_PIN_RST, LOW);
  delay(2);
  pinMode(UWB_PIN_RST, INPUT);
  delay(10);
}

uint32_t readDeviceId() {
  uint8_t rx[4] = {0};
  SPI.beginTransaction(SPISettings(PROBE_SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(UWB_PIN_CS, LOW);
  SPI.transfer(0x00);  // read, register 0x00 (DEV_ID), no sub-index
  for (int i = 0; i < 4; i++) rx[i] = SPI.transfer(0x00);
  digitalWrite(UWB_PIN_CS, HIGH);
  SPI.endTransaction();
  return (uint32_t)rx[3] << 24 | (uint32_t)rx[2] << 16 |
         (uint32_t)rx[1] << 8 | (uint32_t)rx[0];
}

void diagnose(uint32_t id) {
  if (id == DW1000_DEV_ID) {
    Serial.println("  -> OK. The module is alive and the SPI wiring is right.");
    Serial.println("     Next: stage E2 (frame exchange between the two boards).");
    return;
  }
  if (id == 0x00000000UL) {
    Serial.println("  -> All zeros. MISO is stuck low: check the MISO wire, the");
    Serial.println("     CS wire, and that VDD3V3/VDDAON actually read 3.3 V.");
  } else if (id == 0xFFFFFFFFUL) {
    Serial.println("  -> All ones. MISO is floating: the module is probably not");
    Serial.println("     powered, or MISO/CS is disconnected.");
  } else {
    Serial.println("  -> Garbage. Likely SPI mode or clock: confirm MODE0 wiring,");
    Serial.println("     and that MOSI/MISO are not swapped.");
  }
  Serial.println();
  Serial.println("     Before assuming the module is dead, power everything down");
  Serial.println("     and let VDDAON discharge below 100 mV. Re-applying power");
  Serial.println("     while it sits between 100 mV and 2.3 V leaves the DWM1000");
  Serial.println("     in an undefined state that only a full discharge clears");
  Serial.println("     (datasheet section 5.2.3). A module that looks dead may");
  Serial.println("     simply be stuck there.");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== DWM1000 probe (bring-up stage E1) ===");
  Serial.printf("SPI: SCK=18 MISO=19 MOSI=23 CS=%d RST=%d @ %lu Hz, mode 0\n",
                UWB_PIN_CS, UWB_PIN_RST, (unsigned long)PROBE_SPI_HZ);

  pinMode(UWB_PIN_CS, OUTPUT);
  digitalWrite(UWB_PIN_CS, HIGH);
  SPI.begin();
  hardReset();
}

void loop() {
  const uint32_t id = readDeviceId();
  Serial.printf("DEV_ID = 0x%08lX  (expected 0x%08lX)\n",
                (unsigned long)id, (unsigned long)DW1000_DEV_ID);
  diagnose(id);
  Serial.println();
  delay(2000);
}
