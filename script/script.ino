#include <esp_now.h>
#include "WiFi.h"


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

// Called when this board receives data from any ESP-NOW peer
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  Serial.printf("Received %d bytes from %02X:%02X:%02X:%02X:%02X:%02X: ",
    len,
    info->src_addr[0], info->src_addr[1], info->src_addr[2],
    info->src_addr[3], info->src_addr[4], info->src_addr[5]);
  Serial.write(data, len);
  Serial.println();
}

void setup() {
  Serial.begin(115200);

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

// Dummy loop: broadcasts a message every 4s while blinking the LED
void loop() {
  const char *message = "hello from ESP32";
  esp_now_send(broadcastAddress, (const uint8_t *)message, strlen(message));

  digitalWrite(LED_BUILTIN, HIGH);
  delay(2000);

  digitalWrite(LED_BUILTIN, LOW);
  delay(2000);
}
