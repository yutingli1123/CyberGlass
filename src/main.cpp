#include <Arduino.h>
#include "ble_image_transfer.h"
#include "camera_module.h"

// Global instances
BLEImageTransfer bleImageTransfer;

void setup() {
  Serial.begin(115200); // Initialize serial communication
  delay(1000);

  // Initialize system
  Serial.println("\n========================================");
  Serial.println("  XIAO ESP32S3 - CyberGlass System");
  Serial.println("  BLE-Only Mode");
  Serial.println("========================================");

  // Initialize camera module
  if (initCamera()) {
    Serial.println("[" + getTimestamp() + "] Camera module: READY");
  } else {
    Serial.println("[" + getTimestamp() + "] WARNING: Camera initialization failed");
  }

  // Initialize BLE with image transfer support
  if (bleImageTransfer.initBLE()) {
    Serial.println("[" + getTimestamp() + "] BLE Image Transfer: READY");
  } else {
    Serial.println("[" + getTimestamp() + "] WARNING: BLE initialization failed");
  }

  // Print connection information
  Serial.println("\n========================================");
  Serial.println("BLE Device Name: CyberGlass");
  Serial.println("Use a BLE scanner app to:");
  Serial.println("  - Request and transfer images via BLE");
  Serial.println("\nNote: BLE is open for connections (within range ~10m)");
  Serial.println("========================================\n");
}

void loop() {
  // Process pending BLE image requests
  bleImageTransfer.processPendingRequests();

  // Yield to system tasks
  yield();
}
