// Bring-up stage E4: antenna delay calibration.
//
//   pio run -e uwb_calib -t upload && pio device monitor -e uwb_calib
//
// Flash this to BOTH boards, place them a known distance apart (a tape measure
// and a tripod beat guessing), let the statistics settle, then run `cal <m>` on
// each board.
//
// Why this stage is not optional: the DW1000 timestamps at ~15.65 ps, which is
// 4.69 mm of flight time per tick. The antenna delay constant is around 16436
// ticks — some 77 m of apparent distance — so being off by only 213 ticks puts
// a full metre of constant bias on every measurement. The vendor default gets
// the right order of magnitude and nothing more; METRICS.md asks for RMSE
// under 1 m in the 0-5 m band, which cannot be met uncalibrated.
//
// Commands:
//   cal <meters>      apply HALF the correction here, and run it on the other
//                     board too. The measured error is the sum of both boards'
//                     delay errors, so splitting it gives each board its own
//                     share and keeps a third board honest later.
//   calfull <meters>  apply the whole correction here. Use only when the other
//                     board is already calibrated and acts as the reference.
//   set <ticks>       write an antenna delay directly.
//   clear             erase the stored value, reverting to the default.
//   reset             discard collected samples and start over.
//   stats             print statistics now.

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include <DW1000NgConstants.hpp>

#include "comms.h"
#include "config.h"
#include "peers.h"
#include "uwb_twr.h"

namespace {

constexpr int SAMPLE_CAP = 200;

float    samples[SAMPLE_CAP];
int      sampleCount = 0;
int      sampleNext  = 0;
uint32_t lastSampleStamp = 0;
uint8_t  peerMac[6] = {0};
bool     havePeer   = false;

void resetSamples() {
  sampleCount = 0;
  sampleNext  = 0;
}

void addSample(float m) {
  samples[sampleNext] = m;
  sampleNext          = (sampleNext + 1) % SAMPLE_CAP;
  if (sampleCount < SAMPLE_CAP) sampleCount++;
}

bool statistics(float& mean, float& stddev) {
  if (sampleCount < 2) return false;
  double sum = 0;
  for (int i = 0; i < sampleCount; i++) sum += samples[i];
  mean = (float)(sum / sampleCount);
  double acc = 0;
  for (int i = 0; i < sampleCount; i++) {
    const double d = samples[i] - mean;
    acc += d * d;
  }
  stddev = (float)sqrt(acc / (sampleCount - 1));
  return true;
}

void applyCorrection(float trueMeters, bool halve) {
  float mean = 0, stddev = 0;
  if (!statistics(mean, stddev)) {
    Serial.println("Not enough samples yet — let it run a few seconds.");
    return;
  }
  // Reported range grows when the configured antenna delay is too small, so
  // over-reporting is corrected by increasing the delay.
  const float errorMeters = mean - trueMeters;
  float       deltaTicks  = errorMeters * DISTANCE_OF_RADIO_INV;
  if (halve) deltaTicks *= 0.5f;

  const int32_t current = uwb::antennaDelay();
  int32_t       updated = current + (int32_t)lroundf(deltaTicks);
  if (updated < 0)     updated = 0;
  if (updated > 65535) updated = 65535;

  Serial.printf("measured mean %.3f m (sd %.3f) over %d samples, true %.3f m\n",
                mean, stddev, sampleCount, trueMeters);
  if (stddev > 0.15f) {
    Serial.println("WARNING: spread above 15 cm — check line of sight and let it settle.");
  }
  Serial.printf("error %.3f m -> %+ld ticks%s\n", errorMeters,
                (long)lroundf(deltaTicks), halve ? " (half share)" : " (full)");
  Serial.printf("antenna delay %ld -> %ld, saved to NVS\n", (long)current,
                (long)updated);

  uwb::setAntennaDelay((uint16_t)updated, true);
  resetSamples();

  if (halve) {
    Serial.println("Now run the same command on the other board.");
  }
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  if (line.startsWith("cal ")) {
    applyCorrection(line.substring(4).toFloat(), true);
  } else if (line.startsWith("calfull ")) {
    applyCorrection(line.substring(8).toFloat(), false);
  } else if (line.startsWith("set ")) {
    const long ticks = line.substring(4).toInt();
    if (ticks < 0 || ticks > 65535) {
      Serial.println("Antenna delay must be between 0 and 65535 ticks.");
      return;
    }
    uwb::setAntennaDelay((uint16_t)ticks, true);
    Serial.printf("antenna delay set to %ld and saved\n", ticks);
    resetSamples();
  } else if (line == "clear") {
    uwb::clearStoredAntennaDelay();
    Serial.printf("stored calibration erased; %u applies after reboot\n",
                  UWB_ANTENNA_DELAY_DEFAULT);
  } else if (line == "reset") {
    resetSamples();
    Serial.println("samples cleared");
  } else if (line == "stats") {
    float mean = 0, stddev = 0;
    if (statistics(mean, stddev)) {
      Serial.printf("n=%d  mean=%.3f m  sd=%.3f m  antenna delay=%u\n",
                    sampleCount, mean, stddev, uwb::antennaDelay());
    } else {
      Serial.println("Not enough samples yet.");
    }
  } else {
    Serial.println("Commands: cal <m> | calfull <m> | set <ticks> | clear | reset | stats");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== DWM1000 antenna delay calibration (bring-up stage E4) ===");

  peers::init();
  if (!comms::begin()) {
    Serial.println("Comms init failed — halting");
    while (true) delay(1000);
  }
  if (!uwb::begin()) {
    Serial.println("UWB init failed — run the uwb_probe environment first.");
    while (true) delay(1000);
  }

  Serial.println();
  Serial.println("Place the boards a measured distance apart, wait for the");
  Serial.println("standard deviation to settle, then run: cal <metres>");
  Serial.println("Type 'stats' at any time; 'reset' to clear the samples.");
  Serial.println();
}

void loop() {
  while (Serial.available() > 0) {
    handleCommand(Serial.readStringUntil('\n'));
  }

  // Discover the peer over ESP-NOW, exactly as the production firmware does.
  if (!havePeer) {
    PeerState fresh[MAX_PEERS];
    const int n = peers::snapshotFresh(fresh, millis());
    if (n > 0) {
      memcpy(peerMac, fresh[0].mac, 6);
      havePeer = true;
      Serial.printf("peer %02X:%02X:%02X:%02X:%02X:%02X found\n",
                    peerMac[0], peerMac[1], peerMac[2],
                    peerMac[3], peerMac[4], peerMac[5]);
    }
  }

  // Only count genuinely new measurements — polling faster than the ranging
  // cadence would otherwise record the same sample repeatedly and make the
  // standard deviation look far better than it is.
  if (havePeer) {
    float    meters = 0;
    uint32_t stamp  = 0;
    if (uwb::lastRange(peerMac, meters, stamp) && stamp != lastSampleStamp) {
      lastSampleStamp = stamp;
      addSample(meters);
    }
  }

  static uint32_t lastReport = 0;
  const uint32_t  now        = millis();
  if (now - lastReport >= 1000) {
    lastReport = now;
    float mean = 0, stddev = 0;
    if (statistics(mean, stddev)) {
      Serial.printf("n=%3d  mean=%7.3f m  sd=%6.3f m  delay=%u  ok=%lu fail=%lu\n",
                    sampleCount, mean, stddev, uwb::antennaDelay(),
                    (unsigned long)uwb::exchangesOk(),
                    (unsigned long)uwb::exchangesFailed());
    } else if (havePeer) {
      Serial.printf("waiting for ranging... ok=%lu fail=%lu\n",
                    (unsigned long)uwb::exchangesOk(),
                    (unsigned long)uwb::exchangesFailed());
    } else {
      Serial.println("waiting for a peer on ESP-NOW...");
    }
  }

  delay(20);
}
