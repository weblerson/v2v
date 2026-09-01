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

### ESP32 + DWM1000 UWB (ranging)

Distance now comes from Two-Way Ranging over ultra-wideband. The NEO-6M GPS it
replaced is no longer wired — see `docs/PLAN_DWM1000.md`.

| DWM1000 pin  | ESP32 pin | Notes |
|--------------|-----------|-------|
| VDD3V3 (6,7) | 3V3       | **Never 5 V** — absolute maximum is 4.0 V |
| VDDAON (5)   | 3V3       | Same rail |
| GND (8,16,21,23,24) | GND | All of them |
| SPICLK (20)  | GPIO 18   | VSPI |
| SPIMISO (19) | GPIO 19   | VSPI |
| SPIMOSI (18) | GPIO 23   | VSPI |
| SPICSn (17)  | GPIO 5    | Held high at boot by the module's internal pull-up |
| RSTn (3)     | GPIO 27   | **Never drive high.** Firmware only pulls it low, then releases to high-Z |
| IRQ (22)     | GPIO 26   | Unused (polled mode); leave unconnected or tie through a pull-down |
| WAKEUP (2)   | GND       | Unused |

Before powering anything up, work through the electrical checklist in
`docs/PLAN_DWM1000.md` §7 and run the probe:

```
cd ../firmware
pio run -e uwb_probe -t upload && pio device monitor -e uwb_probe
```

A healthy module answers `DEV_ID = 0xDECA0130`. Anything else is wiring or
power, never software.

Add 10 µF + 100 nF of decoupling at the module, and use a powered USB hub or a
≥1 A supply: the ESP32's WiFi bursts plus the DWM1000's 160 mA receive current
come close to what a plain USB 2.0 port will deliver.

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

The radial scale is square-root, not linear: on a linear scale the 5 m danger
zone would take only a tenth of the radius and pack every nearby vehicle into
the centre, which is exactly where UWB is most accurate and where the display
needs to be readable.

Peers are drawn one of two ways:

- **Ring** (`○`) at the measured radius — distance known, direction unknown.
  This is the normal case. A single DWM1000 antenna measures time of flight,
  not direction; angle of arrival needs phase difference across two antennas,
  which the DW1000 does not have. A ring says "somewhere at this distance",
  which is precisely what the hardware knows. Drawing a dot would claim a
  direction that was never measured.
- **Dot** (`●`) at a bearing — only when the firmware reports
  `bearing_valid: true`, which today means the GPS backend was selected for a
  comparison run.

A legend at the bottom lists every peer with its MAC, distance, bearing,
closing speed, time to collision, and motion state.

## Serial Protocol

The monitor expects the ESP32 to emit one JSON object per line (NDJSON) at
115200 baud. Each line describes one peer:

```json
{"mac":"44:17:93:4C:7F:90","distance":3.24,"bearing":0.0,"bearing_valid":false,"closing":1.85,"ttc":1.8,"state":"BRAKING"}
```

### Fields

| Field           | Type   | Unit            | Description                                          |
|-----------------|--------|-----------------|------------------------------------------------------|
| `mac`           | string | —               | Peer MAC address (`XX:XX:XX:XX:XX:XX`)               |
| `distance`      | float  | meters          | Distance to peer                                     |
| `bearing`       | float  | degrees (0–360) | Direction of peer, 0 = ahead, clockwise              |
| `bearing_valid` | bool   | —               | Whether `bearing` carries real information           |
| `closing`       | float  | m/s             | Rate the gap is shrinking; negative means separating |
| `ttc`           | float  | seconds         | Time to collision, or `-1` when not converging       |
| `state`         | string | —               | `IDLE`, `BRAKING`, or `ACCELERATING`                 |

`closing` and `ttc` are what ranging at 10 Hz with ~10 cm of precision buys:
"3.2 m, closing at 1.9 m/s, 1.8 s to contact" is a complete picture of a
developing collision, and a more actionable one than a bearing.

### Notes

- Peers with no fresh distance are not reported at all. The firmware drops a
  range older than 300 ms, and the monitor drops a peer after 500 ms without a
  line — a cached distance from a moving vehicle is worse than none.
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
