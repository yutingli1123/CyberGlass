#include <Arduino.h>
#include "camera_module.h"

#define LED_PIN 21  // Built-in LED pin for XIAO ESP32S3

void setup() {
  Serial.begin(921600);         // Initialize serial communication at high speed
  
  // Initialize camera module
  Serial.println("\n========================================");
  Serial.println("  XIAO ESP32S3 - CyberGlass System");
  Serial.println("========================================");
  
  if (initCamera()) {
    Serial.println("[" + getTimestamp() + "] Camera module: READY");
    Serial.println("Type 'help' or 'h' for camera commands\n");
  } else {
    Serial.println("[" + getTimestamp() + "] WARNING: Camera initialization failed");
    Serial.println("LED blink mode will continue...\n");
  }
}

void loop() {
  // Process camera commands from serial port
  processCameraCommand();
  
  // Small delay to prevent excessive CPU usage
  delay(10);
}