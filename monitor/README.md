# V2V Proximity Monitor

Terminal-based radar display that shows nearby vehicles in a 360° top-down view.
Reads real-time data from an ESP32 V2V device over USB serial (or from mock data
for development).

## Prerequisites

- **Go 1.21+** — [install](https://go.dev/doc/install) or via your package manager
- **ESP32 DevKit V1** with the V2V firmware flashed (see `../firmware/`)
- **USB data cable** (not charge-only) connecting the ESP32 to your computer
- User must be in the `uucp` group for serial port access (Arch Linux):
  ```
  sudo usermod -aG uucp $USER
  ```
  Log out and back in after adding the group.

## Hardware Setup

### ESP32 + MPU6050 (motion detection)

| MPU6050 pin | ESP32 pin  |
|-------------|------------|
| VCC         | 3V3        |
| GND         | GND        |
| SDA         | GPIO 21    |
| SCL         | GPIO 22    |

### ESP32 + NEO-6M GPS (positioning)

| NEO-6M pin | ESP32 pin   |
|------------|-------------|
| VCC        | 3V3         |
| GND        | GND         |
| TX         | GPIO 16 (RX2) |
| RX         | GPIO 17 (TX2) |

### Connection to Computer

Connect the ESP32 to the computer via USB-B cable. It will appear as
`/dev/ttyACM0` (or `/dev/ttyUSB0` depending on the USB-serial chip).

Verify the device is detected:

```
ls /dev/ttyACM0
```

## Building

```
cd monitor
go build -o v2v-monitor .
```

## Usage

### Mock mode (no hardware required)

For development and testing, use the built-in mock data source that simulates
three vehicles moving around:

```
go run . --mock
```

### Serial mode (connected to ESP32)

With the ESP32 flashed and connected via USB:

```
go run . --port /dev/ttyACM0 --baud 115200
```

These are the defaults, so simply running `go run .` is equivalent.

### Flags

| Flag      | Default          | Description                         |
|-----------|------------------|-------------------------------------|
| `--mock`  | `false`          | Use mock data instead of serial     |
| `--port`  | `/dev/ttyACM0`   | Serial port for the ESP32           |
| `--baud`  | `115200`         | Serial baud rate                    |

### Controls

| Key         | Action |
|-------------|--------|
| `q`         | Quit   |
| `Ctrl+C`    | Quit   |

## Display

The radar shows a top-down circular view centered on your vehicle:

- **Red zone** (inner ring) — peer is closer than **5 m**
- **Yellow zone** (middle ring) — peer is between **5 m** and **20 m**
- **Green zone** (outer ring) — peer is farther than **20 m**

Peers appear as colored dots (`●`) at their bearing and proportional distance.
A legend at the bottom lists every peer with its MAC address, distance, bearing,
and motion state (IDLE, BRAKING, ACCELERATING).

## Serial Protocol

The monitor expects the ESP32 to emit one JSON object per line (NDJSON) at
115200 baud. Each line describes one peer:

```json
{"mac":"44:17:93:4C:7F:90","distance":12.3,"bearing":45.0,"state":"BRAKING"}
```

### Fields

| Field      | Type   | Unit              | Description                              |
|------------|--------|-------------------|------------------------------------------|
| `mac`      | string | —                 | Peer MAC address (`XX:XX:XX:XX:XX:XX`)   |
| `distance` | float  | meters            | Distance to peer                         |
| `bearing`  | float  | degrees (0–360)   | Direction of peer, 0 = ahead, clockwise  |
| `state`    | string | —                 | `IDLE`, `BRAKING`, or `ACCELERATING`     |

### Notes

- The firmware currently logs human-readable lines. To use serial mode, the
  firmware must be updated to emit this JSON format (the `SerialSource` in
  `source/serial.go` is stubbed and ready for implementation).
- Stale peers (no update within 1.5 s) are not displayed — a cached position
  from a moving vehicle is unreliable and potentially dangerous.
- Maximum tracked peers: 8 (configurable in the firmware's `config.h`).

## Flashing the Firmware

From the firmware directory:

```
cd ../firmware
pio run -t upload -t monitor
```

If the upload fails with a permission error, ensure the udev rules are installed:

```
sudo curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules \
  -o /etc/udev/rules.d/99-platformio-udev.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Unplug and replug the ESP32 after installing the rules.

## Project Structure

```
monitor/
├── main.go               Entry point, flag parsing, bubbletea program
├── protocol/
│   └── protocol.go       PeerData struct and DataSource interface
├── source/
│   ├── mock.go           Animated fake peers for development
│   └── serial.go         ESP32 serial reader (stub — TODO)
└── radar/
    └── radar.go          Character-based radar renderer
```
