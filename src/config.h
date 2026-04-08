#pragma once

#include <stdint.h>

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

// --- Loop timing ---
constexpr uint32_t LOOP_INTERVAL_MS = 100;
