# Plan: Relative Distance Measurement Between Devices

## Goal
Calculate the relative distance between two ESP32 devices in real time.

## Options Evaluated

### 1. RSSI (No extra hardware)
- Use `info->rx_ctrl->rssi` from ESP-NOW receive callback
- Estimate distance via log-distance path loss model:
  `distance = 10 ^ ((measuredPower - rssi) / (10 * n))`
  - `measuredPower`: RSSI at 1m (calibrate per environment, typically -40 to -50 dBm)
  - `n`: path loss exponent (2.0 free space, 2.5-4.0 indoors)
- Pros: zero extra cost, already available
- Cons: noisy (+-5m), affected by obstacles/orientation/environment
- Viable as a rough proximity indicator (near/mid/far zones), not precise measurement

### 2. UWB - Ultra-Wideband (Recommended for accuracy)
- Module: DWM1000 or DWM3000 (SPI interface)
- Accuracy: ~10cm using Two-Way Ranging (TWR)
- One device acts as anchor, the other as tag (or symmetric TWR)
- Library: `arduino-dw1000` or Decawave's own SDK
- Pros: cm-level accuracy, works well for V2V range
- Cons: extra module (~$5-15 each), SPI wiring, more code complexity

### 3. GPS (Good for longer range)
- Module: NEO-6M or NEO-M8N (UART interface)
- Each device reads its own GPS coordinates and broadcasts them via ESP-NOW
- Receiver computes Haversine distance
- Accuracy: 2-5m (civilian GPS)
- Pros: works at any range, absolute positioning
- Cons: extra module, no indoor use, slow update rate (~1Hz), 2-5m error

### 4. LiDAR/ToF (Short range only)
- Module: VL53L0X (I2C)
- Accuracy: ~1cm, but max range ~2m
- Not suitable for vehicle-to-vehicle distances

## Recommendation
- For a quick prototype: implement RSSI-based zones (close/medium/far)
- For production accuracy: add UWB modules (DWM1000)
- For outdoor long-range: add GPS modules

## Implementation Steps (RSSI - quickest path)
1. In `onDataRecv`, extract RSSI: `int rssi = info->rx_ctrl->rssi;`
2. Add calibration constants (`measuredPower`, `n`) at the top of the file
3. Write `float estimateDistance(int rssi)` using the log-distance formula
4. Optionally smooth with a moving average filter (5-10 samples) to reduce noise
5. Print/broadcast the estimated distance alongside the motion state

## Implementation Steps (UWB - accurate path)
1. Wire DWM1000 to each ESP32 via SPI
2. Install `arduino-dw1000` library
3. Implement TWR (Two-Way Ranging) protocol between the two devices
4. Read distance from the ranging result
5. Integrate with existing ESP-NOW data (send distance alongside acceleration)
