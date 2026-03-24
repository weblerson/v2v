#include <esp_now.h>
#include "WiFi.h"
#include "MPU6050.h"
#include "Wire.h"

MPU6050 mpu;

// Relative motion state between this device and the remote device
enum MotionState { IDLE, ACCELERATING, BRAKING };

// Threshold in raw accelerometer units (±2g range: 16384 LSB/g)
// 0.15g * 16384 = ~2458
const int16_t ACCEL_THRESHOLD = 2458;

// Baseline X-axis reading at rest (calibrated in setup)
int16_t baselineX = 0;

// Latest local forward acceleration (baseline-corrected)
volatile int16_t localAccel = 0;

// Latest remote forward acceleration received via ESP-NOW
volatile int16_t remoteAccel = 0;
volatile bool remoteDataReceived = false;

// Read this device's forward acceleration (X-axis, baseline-corrected)
int16_t readLocalAccel() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  return ax - baselineX;
}

// Determine relative motion state.
// relativeAccel = remoteAccel - localAccel
//   negative → remote is decelerating relative to us → gap closing → BRAKING
//   positive → remote is accelerating relative to us → gap growing → ACCELERATING
MotionState detectRelativeMotion() {
  int16_t relativeAccel = remoteAccel - localAccel;

  if (relativeAccel < -ACCEL_THRESHOLD) {
    return BRAKING;
  } else if (relativeAccel > ACCEL_THRESHOLD) {
    return ACCELERATING;
  }
  return IDLE;
}

// Calibrate baseline by averaging samples while the device is stationary
void calibrateMPU(int samples = 100) {
  long sumX = 0;
  for (int i = 0; i < samples; i++) {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    sumX += ax;
    delay(10);
  }
  baselineX = sumX / samples;
  Serial.printf("MPU6050 calibrated. Baseline X: %d\n", baselineX);
}

uint8_t macAddress[6];
// Broadcast address: sends to all ESP-NOW peers on the same channel
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// --- ESP-NOW Callbacks ---
// TODO: move these to a separate .h/.cpp file

// Called after each esp_now_send() completes
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.printf("Send status: %s\n",
    status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// Called when this board receives data from any ESP-NOW peer.
// Expects a raw int16_t representing the remote device's forward acceleration.
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len == sizeof(int16_t)) {
    memcpy((void *)&remoteAccel, data, sizeof(int16_t));
    remoteDataReceived = true;
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize MPU6050
  Wire.begin();
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 connection failed");
    return;
  }
  Serial.println("MPU6050 connected");

  // Calibrate at rest — keep the device still during startup
  calibrateMPU();

  // WiFi must be in station mode for ESP-NOW to work
  WiFi.mode(WIFI_STA);

  // Store this board's MAC for potential peer registration
  WiFi.macAddress(macAddress);
  Serial.printf("\nMAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
    macAddress[0], macAddress[1], macAddress[2],
    macAddress[3], macAddress[4], macAddress[5]);

  // Initialize ESP-NOW protocol
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  Serial.println("Successfully initialized ESP-NOW");

  // Register send/receive handlers
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);
  Serial.println("Registered handlers.");

  // Register the broadcast address as a peer so we can send to all boards
  // without needing to know their individual MACs
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;   // use current WiFi channel
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add broadcast peer");
    return;
  }
  Serial.println("Broadcast peer added.");

  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  // Read and broadcast our own forward acceleration
  localAccel = readLocalAccel();
  esp_now_send(broadcastAddress, (const uint8_t *)&localAccel, sizeof(localAccel));

  // Compute relative motion only if we've heard from the other device
  if (remoteDataReceived) {
    MotionState state = detectRelativeMotion();

    switch (state) {
      case BRAKING:
        Serial.println("Relative: BRAKING (vehicle ahead slowing down)");
        digitalWrite(LED_BUILTIN, HIGH);
        break;
      case ACCELERATING:
        Serial.println("Relative: ACCELERATING (vehicle ahead speeding up)");
        digitalWrite(LED_BUILTIN, LOW);
        break;
      default:
        digitalWrite(LED_BUILTIN, LOW);
        break;
    }
  }

  delay(100);
}
