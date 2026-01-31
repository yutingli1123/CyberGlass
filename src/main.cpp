#include <Arduino.h>
#include "ble_video_stream.h"
#include "camera_module.h"

// Global instances
BLEVideoStream bleVideoStream;

void setup() {
  Serial.begin(115200); // Initialize serial communication
  // delay(1000);

  // Set initial CPU frequency to 80MHz for power saving
  setCpuFrequencyMhz(80);


  // Initialize system
  Serial.println("\n========================================");
  Serial.println("  XIAO ESP32S3 - CyberGlass System");
  Serial.println("========================================");

  // Initialize camera module
  if (initCamera()) {
    Serial.println("[" + getTimestamp() + "] Camera module: READY");
  } else {
    Serial.println("[" + getTimestamp() + "] WARNING: Camera initialization failed");
  }

  // Initialize BLE with video stream support
  if (bleVideoStream.initBLE()) {
    Serial.println("[" + getTimestamp() + "] BLE Video Stream: READY");
  } else {
    Serial.println("[" + getTimestamp() + "] WARNING: BLE initialization failed");
  }

  // Print connection information
  Serial.println("\n========================================");
  Serial.println("BLE Device Name: " + bleVideoStream.getDeviceName());
  Serial.println("Use a BLE scanner app to:");
  Serial.println("  - Start and stop the video stream");
  Serial.println("\nNote: BLE is open for connections (within range ~10m)");
  Serial.println("========================================\n");
}

void loop() {
  // Process video stream (if active)
  bleVideoStream.processVideoStream();

  // Power saving: if video is not streaming, sleep longer
  if (!bleVideoStream.isVideoStreamActive()) {
    delay(100); // Light sleep during idle
  } else {
    // Yield to system tasks
    yield();
  }
}
