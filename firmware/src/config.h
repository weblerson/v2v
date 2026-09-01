#pragma once

#include <stdint.h>

// --- Debug ---
// Set to 1 to print raw sensor data (GPS NMEA, MPU6050 accel) to Serial.
#define DEBUG 0
// --- Motion detection ---
// MPU6050 ±2g range: 16384 LSB/g. 0.15g ≈ 2458 LSB.
constexpr int16_t ACCEL_THRESHOLD       = 2458;
// Hysteresis: once a state is entered, only leave it when |relAccel| drops
// below THRESHOLD * EXIT_RATIO. Prevents flapping around the boundary.
constexpr float   HYSTERESIS_EXIT_RATIO = 0.5f;
// Moving-average window applied to local accel before comparing.
constexpr int     SMOOTHING_WINDOW      = 5;
// Calibration sample count at boot (device must be at rest).
constexpr int     CALIBRATION_SAMPLES   = 100;

// --- Peer table ---
constexpr int      MAX_PEERS         = 8;
// A peer is considered stale (and ignored) after this many ms without packets.
constexpr uint32_t REMOTE_TIMEOUT_MS = 500;
// Reboot detection: a peer that restarts resets its seq counter to 1, which
// would otherwise be rejected forever by the monotonic-seq rule. A backward
// jump larger than this is read as "the peer rebooted", not as a stale packet.
// (The primary signal is inactivity — see peers::upsert.)
constexpr uint32_t SEQ_REBOOT_GAP    = 100;

// --- Loop timing ---
constexpr uint32_t LOOP_INTERVAL_MS = 100;

// --- Positioning ---
// Max age for GPS/peer position data. Older than this → treated as
// "no data", never returned to the caller. Deliberately short: a moving
// vehicle's last-known position becomes dangerously misleading quickly.
constexpr uint32_t POSITION_MAX_AGE_MS = 1500;

// NEO-6M default UART baud rate.
// The GPS backend is no longer the active one (UWB replaced it — see
// docs/PLAN_DWM1000.md, D10) but is kept compiling so the metrics campaign can
// compare both backends on the same runs.
constexpr uint32_t GPS_BAUD   = 9600;
constexpr int      GPS_RX_PIN = 17;  // ESP32 RX — wire to GPS TX
constexpr int      GPS_TX_PIN = 16;  // ESP32 TX — wire to GPS RX

// --- UWB (DWM1000) ---
// SPI runs on the ESP32's default VSPI pins, which is what SPI.begin() selects
// and what the DWM1000 wiring assumes: SCK=18, MISO=19, MOSI=23.
constexpr int UWB_PIN_CS  = 5;   // SPICSn. Strapping pin, but the DWM1000's
                                 // internal ~60k pull-up holds it high at boot.
constexpr int UWB_PIN_RST = 27;  // RSTn. NEVER driven high — the driver only
                                 // pulls it low and returns it to high-Z.
constexpr int UWB_PIN_IRQ = 26;  // Unused in polled mode; reserved.

// PAN ID shared by every vehicle in the fleet.
constexpr uint16_t UWB_PAN_ID = 0xDECA;

// Antenna delay in DW1000 ticks (1 tick ≈ 15.65 ps ≈ 4.69 mm).
// 16436 is the vendor's typical value and only a starting point: ~213 ticks of
// error is 1 m of constant range bias, so each board must be calibrated with
// the `uwb_calib` environment. The calibrated value lives in NVS and overrides
// this default at boot.
constexpr uint16_t UWB_ANTENNA_DELAY_DEFAULT = 16436;

// Target interval between range measurements against the same peer.
constexpr uint32_t UWB_RANGE_PERIOD_MS = 100;
// Hard deadline for one full four-frame exchange. Generous: a complete
// exchange is ~8 ms.
constexpr uint32_t UWB_EXCHANGE_TIMEOUT_MS = 30;
// Delay between receiving a frame and transmitting the scheduled reply. The
// DW1000 itself emits at the programmed instant, so this only has to exceed
// the MCU's worst-case turnaround (WiFi interrupts included). Lower it while
// watching for HPDWARN once the link is proven.
constexpr uint16_t UWB_REPLY_DELAY_US = 3000;
// Consecutive failures before a peer is de-prioritised in the round-robin.
constexpr int UWB_MAX_FAILURES = 3;

// Freshness window for a UWB range. Much tighter than the GPS-era constant
// below: at 50 km/h (~14 m/s) even 300 ms is 4 m of travel, and ranging runs
// at 10 Hz, so three missed exchanges is already a real gap.
constexpr uint32_t UWB_RANGE_MAX_AGE_MS = 300;

// --- Closing speed / time-to-collision ---
// Samples used for the least-squares slope of distance over time. 5 samples at
// 10 Hz is a 0.5 s window: long enough to reject ranging noise, short enough
// to track a real deceleration.
constexpr int   RATE_WINDOW  = 5;
// Below this closing speed the pair is not meaningfully converging and TTC is
// reported as unavailable rather than as a huge number.
constexpr float MIN_CLOSING_SPEED_MPS = 0.3f;
// Reporting cap for TTC, in seconds.
constexpr float TTC_MAX_S = 99.9f;
