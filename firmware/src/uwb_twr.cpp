#include "uwb_twr.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include <DW1000Ng.hpp>
#include <DW1000NgConstants.hpp>
#include <DW1000NgRanging.hpp>
#include <DW1000NgTime.hpp>
#include <DW1000NgUtils.hpp>

#include "comms.h"
#include "peers.h"

namespace {

// --- Frame layout -----------------------------------------------------------
//
// Every frame is the same fixed size. Variable-length frames would shave ~120 us
// of airtime off the shorter ones, which is irrelevant at our duty cycle, and
// a single layout removes a whole class of parsing bugs.
//
//   [0]      type
//   [1..2]   source short address
//   [3..4]   destination short address
//   [5..10]  source MAC (the ESP-NOW identity — the key everything else uses)
//   [11..15] timestamp 1   / range in millimetres for RANGE_REPORT
//   [16..20] timestamp 2
//   [21..25] timestamp 3
//   [26..27] spare
constexpr uint8_t FRAME_LEN     = 28;
constexpr uint8_t OFF_TYPE      = 0;
constexpr uint8_t OFF_SRC_ADDR  = 1;
constexpr uint8_t OFF_DST_ADDR  = 3;
constexpr uint8_t OFF_SRC_MAC   = 5;
constexpr uint8_t OFF_TS1       = 11;
constexpr uint8_t OFF_TS2       = 16;
constexpr uint8_t OFF_TS3       = 21;
constexpr uint8_t OFF_RANGE_MM  = 11;

enum FrameType : uint8_t {
  FRAME_POLL         = 0x21,
  FRAME_POLL_ACK     = 0x10,
  FRAME_RANGE        = 0x23,
  FRAME_RANGE_REPORT = 0x2A,
};

// Radio configuration. Channel 5 (6489.6 MHz) does not overlap the 2.4 GHz
// band ESP-NOW uses, so the two radios do not interfere.
//
// 850 kbps with a 256-symbol preamble is the deliberate middle ground: 6.8 Mbps
// would cut airtime but costs ~8 dB of receiver sensitivity (-93 vs -101 dBm,
// datasheet Table 8), and range matters more than airtime at our duty cycle.
//
// receiverAutoReenable is off on purpose. The chip staying idle after a
// reception means the RX buffer and timestamp cannot be overwritten between
// isReceiveDone() and reading them out.
const device_configuration_t RADIO_CONFIG = {
    false,                      // extendedFrameLength
    false,                      // receiverAutoReenable
    true,                       // smartPower
    true,                       // frameCheck (CRC)
    false,                      // nlos
    SFDMode::STANDARD_SFD,
    Channel::CHANNEL_5,
    DataRate::RATE_850KBPS,
    PulseFrequency::FREQ_16MHZ,
    PreambleLength::LEN_256,
    PreambleCode::CODE_3};

// --- Result table -----------------------------------------------------------

struct RangeSlot {
  uint8_t  mac[6];
  float    meters;
  uint32_t lastOkMs;
  uint32_t lastAttemptMs;
  uint8_t  failures;
  bool     hasRange;
  bool     used;
};

RangeSlot    slots[MAX_PEERS];
portMUX_TYPE slotMux = portMUX_INITIALIZER_UNLOCKED;

uint8_t  selfMac[6]  = {0};
uint16_t selfAddr    = 0;
uint16_t activeAntennaDelay = UWB_ANTENNA_DELAY_DEFAULT;
byte     txBuf[FRAME_LEN];
byte     rxBuf[FRAME_LEN];
int      rrCursor    = 0;

volatile uint32_t okCount   = 0;
volatile uint32_t failCount = 0;

bool macEqual(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

uint16_t addr16Of(const uint8_t mac[6]) {
  return (uint16_t)((uint16_t)mac[4] << 8 | mac[5]);
}

// Deterministic role assignment: the lexicographically smaller MAC initiates.
// Both sides end up with the distance regardless, because the exchange closes
// with a RANGE_REPORT.
bool iAmInitiatorFor(const uint8_t peerMac[6]) {
  return memcmp(selfMac, peerMac, 6) < 0;
}

// Find the slot for `mac`, allocating (or evicting the stalest) if needed.
// Caller must hold slotMux.
int slotFor(const uint8_t mac[6], uint32_t nowMs) {
  for (int i = 0; i < MAX_PEERS; i++) {
    if (slots[i].used && macEqual(slots[i].mac, mac)) return i;
  }
  int      target    = -1;
  uint32_t oldestAge = 0;
  int      oldestIdx = 0;
  for (int i = 0; i < MAX_PEERS; i++) {
    if (!slots[i].used) { target = i; break; }
    const uint32_t age = nowMs - slots[i].lastOkMs;
    if (age >= oldestAge) { oldestAge = age; oldestIdx = i; }
  }
  if (target < 0) target = oldestIdx;
  memset(&slots[target], 0, sizeof(RangeSlot));
  memcpy(slots[target].mac, mac, 6);
  slots[target].used = true;
  return target;
}

void recordSuccess(const uint8_t mac[6], float meters, uint32_t nowMs) {
  portENTER_CRITICAL(&slotMux);
  const int i = slotFor(mac, nowMs);
  slots[i].meters        = meters;
  slots[i].lastOkMs      = nowMs;
  slots[i].lastAttemptMs = nowMs;
  slots[i].failures      = 0;
  slots[i].hasRange      = true;
  portEXIT_CRITICAL(&slotMux);
  okCount++;
}

void recordFailure(const uint8_t mac[6], uint32_t nowMs) {
  portENTER_CRITICAL(&slotMux);
  const int i = slotFor(mac, nowMs);
  slots[i].lastAttemptMs = nowMs;
  if (slots[i].failures < 255) slots[i].failures++;
  portEXIT_CRITICAL(&slotMux);
  failCount++;
}

// --- Radio helpers ----------------------------------------------------------

// Microsecond deadline that survives millis()/micros() rollover.
struct Deadline {
  uint32_t start;
  uint32_t limit;
  explicit Deadline(uint32_t limitUs) : start(micros()), limit(limitUs) {}
  bool expired() const { return (uint32_t)(micros() - start) > limit; }
};

void listen() {
  DW1000Ng::forceTRxOff();
  DW1000Ng::clearReceiveStatus();
  DW1000Ng::startReceive();
}

void abortExchange() {
  DW1000Ng::forceTRxOff();
  DW1000Ng::clearReceiveStatus();
  DW1000Ng::clearReceiveFailedStatus();
  DW1000Ng::clearReceiveTimeoutStatus();
  DW1000Ng::clearTransmitStatus();
}

void beginFrame(uint8_t type, const uint8_t dstMac[6]) {
  memset(txBuf, 0, FRAME_LEN);
  txBuf[OFF_TYPE]         = type;
  txBuf[OFF_SRC_ADDR]     = (uint8_t)(selfAddr & 0xFF);
  txBuf[OFF_SRC_ADDR + 1] = (uint8_t)(selfAddr >> 8);
  const uint16_t dst      = addr16Of(dstMac);
  txBuf[OFF_DST_ADDR]     = (uint8_t)(dst & 0xFF);
  txBuf[OFF_DST_ADDR + 1] = (uint8_t)(dst >> 8);
  memcpy(txBuf + OFF_SRC_MAC, selfMac, 6);
}

// Transmit the staged frame and spin until the chip confirms it left.
// The spin is tight on purpose: each iteration is an SPI status read, so the
// effective poll period is tens of microseconds. Reply turnaround directly
// affects the residual clock error in the ranging maths, and a 1 ms task tick
// would be far too coarse.
bool transmitAndWait(TransmitMode mode, const Deadline& dl) {
  DW1000Ng::setTransmitData(txBuf, FRAME_LEN);
  DW1000Ng::startTransmit(mode);
  while (!dl.expired()) {
    if (DW1000Ng::isTransmitDone()) {
      DW1000Ng::clearTransmitStatus();
      return true;
    }
  }
  return false;
}

// Wait for a frame of `type` from `fromMac` addressed to us. Frames that do
// not match are dropped and reception restarts — with several vehicles around,
// another pair's traffic is expected, not an error.
//
// Assumes the receiver is already armed. Callers do that themselves, the
// instant a transmission completes and before reading its timestamp back: the
// peer replies as fast as it can, and every SPI round-trip spent before
// arming is margin given away.
bool awaitFrame(uint8_t type, const uint8_t fromMac[6], const Deadline& dl) {
  while (!dl.expired()) {
    if (!DW1000Ng::isReceiveDone()) continue;
    DW1000Ng::clearReceiveStatus();
    DW1000Ng::getReceivedData(rxBuf, FRAME_LEN);

    const uint16_t dst =
        (uint16_t)rxBuf[OFF_DST_ADDR] | (uint16_t)rxBuf[OFF_DST_ADDR + 1] << 8;
    if (rxBuf[OFF_TYPE] == type && dst == selfAddr &&
        macEqual(rxBuf + OFF_SRC_MAC, fromMac)) {
      return true;
    }
    DW1000Ng::startReceive();
  }
  return false;
}

// --- Initiator --------------------------------------------------------------
//
//   POLL  ──────────────►
//         ◄────────────── POLL_ACK
//   RANGE ──────────────►   (delayed TX: we must know when it leaves before
//         ◄────────────── RANGE_REPORT   we can put that timestamp inside it)
//
// The responder holds all six timestamps and does the arithmetic, so the
// distance comes back to us in the report.
bool runInitiator(const uint8_t peerMac[6]) {
  const Deadline dl(UWB_EXCHANGE_TIMEOUT_MS * 1000UL);

  DW1000Ng::forceTRxOff();

  beginFrame(FRAME_POLL, peerMac);
  if (!transmitAndWait(TransmitMode::IMMEDIATE, dl)) return false;
  DW1000Ng::startReceive();
  const uint64_t timePollSent = DW1000Ng::getTransmitTimestamp();

  if (!awaitFrame(FRAME_POLL_ACK, peerMac, dl)) return false;
  const uint64_t timePollAckReceived = DW1000Ng::getReceiveTimestamp();

  // Schedule the RANGE frame for a known future instant so its transmit
  // timestamp can be embedded in the frame itself. The DW1000 emits at exactly
  // that instant, which is why MCU jitter (WiFi interrupts included) cannot
  // corrupt the measurement — at worst the window is missed and we retry.
  uint64_t timeRangeSent =
      DW1000Ng::getSystemTimestamp() +
      DW1000NgTime::microsecondsToUWBTime(UWB_REPLY_DELAY_US);
  byte futureTimeBytes[LENGTH_TIMESTAMP];
  DW1000NgUtils::writeValueToBytes(futureTimeBytes, timeRangeSent,
                                   LENGTH_TIMESTAMP);
  DW1000Ng::setDelayedTRX(futureTimeBytes);
  timeRangeSent += DW1000Ng::getTxAntennaDelay();

  beginFrame(FRAME_RANGE, peerMac);
  DW1000NgUtils::writeValueToBytes(txBuf + OFF_TS1, timePollSent,
                                   LENGTH_TIMESTAMP);
  DW1000NgUtils::writeValueToBytes(txBuf + OFF_TS2, timePollAckReceived,
                                   LENGTH_TIMESTAMP);
  DW1000NgUtils::writeValueToBytes(txBuf + OFF_TS3, timeRangeSent,
                                   LENGTH_TIMESTAMP);
  if (!transmitAndWait(TransmitMode::DELAYED, dl)) return false;
  DW1000Ng::startReceive();

  if (!awaitFrame(FRAME_RANGE_REPORT, peerMac, dl)) return false;

  const uint32_t mm = (uint32_t)rxBuf[OFF_RANGE_MM] |
                      (uint32_t)rxBuf[OFF_RANGE_MM + 1] << 8 |
                      (uint32_t)rxBuf[OFF_RANGE_MM + 2] << 16 |
                      (uint32_t)rxBuf[OFF_RANGE_MM + 3] << 24;
  recordSuccess(peerMac, (float)mm / 1000.0f, millis());
  return true;
}

// --- Responder --------------------------------------------------------------
//
// Entered when a POLL addressed to us has already been received and parsed;
// the caller passes in its reception timestamp.
bool runResponder(const uint8_t peerMac[6], uint64_t timePollReceived) {
  const Deadline dl(UWB_EXCHANGE_TIMEOUT_MS * 1000UL);

  beginFrame(FRAME_POLL_ACK, peerMac);
  if (!transmitAndWait(TransmitMode::IMMEDIATE, dl)) return false;
  DW1000Ng::startReceive();
  const uint64_t timePollAckSent = DW1000Ng::getTransmitTimestamp();

  if (!awaitFrame(FRAME_RANGE, peerMac, dl)) return false;
  const uint64_t timeRangeReceived = DW1000Ng::getReceiveTimestamp();

  const uint64_t timePollSent = DW1000NgUtils::bytesAsValue(rxBuf + OFF_TS1,
                                                           LENGTH_TIMESTAMP);
  const uint64_t timePollAckReceived =
      DW1000NgUtils::bytesAsValue(rxBuf + OFF_TS2, LENGTH_TIMESTAMP);
  const uint64_t timeRangeSent = DW1000NgUtils::bytesAsValue(rxBuf + OFF_TS3,
                                                             LENGTH_TIMESTAMP);

  double meters = DW1000NgRanging::computeRangeAsymmetric(
      timePollSent, timePollReceived, timePollAckSent, timePollAckReceived,
      timeRangeSent, timeRangeReceived);
  // Must run before any further reception: the correction reads the signal
  // strength registers of the frame that was just received.
  meters = DW1000NgRanging::correctRange(meters);

  // A negative or absurd result means the exchange was corrupted (a lost frame
  // that still passed CRC, a missed delayed-TX window). Reporting it would put
  // a bogus distance on the radar, so it is dropped instead.
  if (meters < 0.0 || meters > 1000.0) return false;

  const uint32_t mm = (uint32_t)(meters * 1000.0);
  beginFrame(FRAME_RANGE_REPORT, peerMac);
  txBuf[OFF_RANGE_MM]     = (uint8_t)(mm & 0xFF);
  txBuf[OFF_RANGE_MM + 1] = (uint8_t)(mm >> 8 & 0xFF);
  txBuf[OFF_RANGE_MM + 2] = (uint8_t)(mm >> 16 & 0xFF);
  txBuf[OFF_RANGE_MM + 3] = (uint8_t)(mm >> 24 & 0xFF);
  if (!transmitAndWait(TransmitMode::IMMEDIATE, dl)) return false;

  recordSuccess(peerMac, (float)meters, millis());
  return true;
}

// Listen for up to `sliceMs`, handling one POLL if it arrives.
void serveResponder(uint32_t sliceMs) {
  const Deadline dl(sliceMs * 1000UL);
  DW1000Ng::startReceive();
  while (!dl.expired()) {
    if (!DW1000Ng::isReceiveDone()) continue;
    DW1000Ng::clearReceiveStatus();
    DW1000Ng::getReceivedData(rxBuf, FRAME_LEN);
    const uint64_t timePollReceived = DW1000Ng::getReceiveTimestamp();

    const uint16_t dst =
        (uint16_t)rxBuf[OFF_DST_ADDR] | (uint16_t)rxBuf[OFF_DST_ADDR + 1] << 8;
    if (rxBuf[OFF_TYPE] == FRAME_POLL && dst == selfAddr) {
      uint8_t peerMac[6];
      memcpy(peerMac, rxBuf + OFF_SRC_MAC, 6);
      if (!runResponder(peerMac, timePollReceived)) {
        recordFailure(peerMac, millis());
        abortExchange();
      }
      return;
    }
    DW1000Ng::startReceive();
  }
}

// --- Scheduling -------------------------------------------------------------

// Next peer we owe a measurement to, or -1. Peers that keep failing are backed
// off so one unreachable vehicle cannot monopolise the round-robin.
int pickDueTarget(const PeerState* fresh, int n, uint32_t nowMs,
                  uint8_t outMac[6]) {
  for (int step = 1; step <= n; step++) {
    const int i = (rrCursor + step) % n;
    if (!iAmInitiatorFor(fresh[i].mac)) continue;

    uint32_t period = UWB_RANGE_PERIOD_MS;
    portENTER_CRITICAL(&slotMux);
    const int s = slotFor(fresh[i].mac, nowMs);
    if (slots[s].failures >= UWB_MAX_FAILURES) period *= 5;
    const uint32_t sinceAttempt = nowMs - slots[s].lastAttemptMs;
    portEXIT_CRITICAL(&slotMux);

    if (sinceAttempt >= period) {
      rrCursor = i;
      memcpy(outMac, fresh[i].mac, 6);
      return i;
    }
  }
  return -1;
}

void rangingTask(void*) {
  PeerState fresh[MAX_PEERS];
  listen();

  for (;;) {
    const uint32_t nowMs = millis();
    const int      n     = peers::snapshotFresh(fresh, nowMs);

    uint8_t targetMac[6];
    const int target = (n > 0) ? pickDueTarget(fresh, n, nowMs, targetMac) : -1;

    if (target >= 0) {
      if (!runInitiator(targetMac)) {
        recordFailure(targetMac, millis());
        abortExchange();
      }
      listen();
    } else {
      // Nothing due — spend the time listening for other vehicles' polls.
      serveResponder(5);
    }
    // Yield so the Arduino loop task and the idle task get the core back.
    vTaskDelay(1);
  }
}

}  // namespace

namespace uwb {

bool begin() {
  memset(slots, 0, sizeof(slots));
  memcpy(selfMac, comms::localMac(), 6);
  selfAddr = addr16Of(selfMac);

  // Polled mode: no interrupt pin, no ISR. Doing SPI from an ISR on the ESP32
  // is the classic way to make this library misbehave, and the ranging task
  // gives us somewhere better to poll from.
  DW1000Ng::initializeNoInterrupt(UWB_PIN_CS, UWB_PIN_RST);

  // Device ID is the go/no-go check: the DW1000 answers 0xDECA0130, so the
  // printable form starts with "DECA". Anything else means the radio is not
  // talking — wiring, power or a dead module, never a software problem.
  // (`uwb_probe` prints the raw 32-bit register for bring-up.)
  char deviceId[128] = {0};
  DW1000Ng::getPrintableDeviceIdentifier(deviceId);
  if (strncmp(deviceId, "DECA", 4) != 0) {
    Serial.printf("UWB: DW1000 not responding (DEV_ID reads \"%s\", expected DECA...)\n",
                  deviceId);
    return false;
  }

  DW1000Ng::applyConfiguration(RADIO_CONFIG);
  DW1000Ng::setNetworkId(UWB_PAN_ID);
  DW1000Ng::setDeviceAddress(selfAddr);

  // Antenna delay dominates the constant part of the range error: ~213 ticks
  // is a whole metre. The per-board value comes from the uwb_calib
  // environment; the vendor default only gets you in the right neighbourhood.
  Preferences prefs;
  activeAntennaDelay = UWB_ANTENNA_DELAY_DEFAULT;
  if (prefs.begin("uwb", true)) {
    activeAntennaDelay = prefs.getUShort("antdelay", UWB_ANTENNA_DELAY_DEFAULT);
    prefs.end();
  }
  DW1000Ng::setAntennaDelay(activeAntennaDelay);

  Serial.printf("UWB: DWM1000 ready, addr=0x%04X, antenna delay=%u%s\n",
                selfAddr, activeAntennaDelay,
                activeAntennaDelay == UWB_ANTENNA_DELAY_DEFAULT
                    ? " (UNCALIBRATED — run the uwb_calib environment)"
                    : "");

  // Pinned to core 1 alongside the Arduino loop task, at a higher priority.
  // Core 0 belongs to the WiFi/ESP-NOW stack; keeping SPI off it avoids
  // fighting the radio driver for the bus and the CPU.
  xTaskCreatePinnedToCore(rangingTask, "uwb_twr", 4096, nullptr, 2, nullptr, 1);
  return true;
}

bool rangeTo(const uint8_t mac[6], uint32_t nowMs, float& meters) {
  bool found = false;
  portENTER_CRITICAL(&slotMux);
  for (int i = 0; i < MAX_PEERS; i++) {
    if (!slots[i].used || !slots[i].hasRange) continue;
    if (!macEqual(slots[i].mac, mac)) continue;
    if ((uint32_t)(nowMs - slots[i].lastOkMs) <= UWB_RANGE_MAX_AGE_MS) {
      meters = slots[i].meters;
      found  = true;
    }
    break;
  }
  portEXIT_CRITICAL(&slotMux);
  return found;
}

bool lastRange(const uint8_t mac[6], float& meters, uint32_t& measuredAtMs) {
  bool found = false;
  portENTER_CRITICAL(&slotMux);
  for (int i = 0; i < MAX_PEERS; i++) {
    if (!slots[i].used || !slots[i].hasRange) continue;
    if (!macEqual(slots[i].mac, mac)) continue;
    meters       = slots[i].meters;
    measuredAtMs = slots[i].lastOkMs;
    found        = true;
    break;
  }
  portEXIT_CRITICAL(&slotMux);
  return found;
}

uint32_t exchangesOk()     { return okCount; }
uint32_t exchangesFailed() { return failCount; }

uint16_t antennaDelay() { return activeAntennaDelay; }

void setAntennaDelay(uint16_t ticks, bool persist) {
  activeAntennaDelay = ticks;
  DW1000Ng::setAntennaDelay(ticks);
  if (persist) {
    Preferences prefs;
    if (prefs.begin("uwb", false)) {
      prefs.putUShort("antdelay", ticks);
      prefs.end();
    }
  }
}

void clearStoredAntennaDelay() {
  Preferences prefs;
  if (prefs.begin("uwb", false)) {
    prefs.remove("antdelay");
    prefs.end();
  }
}

}  // namespace uwb
