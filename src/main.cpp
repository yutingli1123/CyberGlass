#include <Arduino.h>
#include "camera_module.h"
#include "wifi_provisioning.h"
#include "web_server.h"
#include <ESPAsyncWebServer.h>
#include "ble_image_service.h"

#define LED_PIN 21  // Built-in LED pin for XIAO ESP32S3

// Global instances
WiFiProvisioning wifiAP;
AsyncWebServer server(80);
WebServerManager webServer(&server, &wifiAP);
BLEImageService bleImageService;

void setup() {
  Serial.begin(921600);         // Initialize serial communication at high speed
  delay(1000);

  // Initialize system
  Serial.println("\n========================================");
  Serial.println("  XIAO ESP32S3 - CyberGlass System");
  Serial.println("========================================");

  // Initialize WiFi Access Point (generates unique SSID/password)
  if (wifiAP.initAP()) {
    Serial.println("[" + getTimestamp() + "] WiFi AP: READY");
  } else {
    Serial.println("[" + getTimestamp() + "] WARNING: WiFi AP initialization failed");
  }

  // Initialize BLE for provisioning
  if (wifiAP.initBLE()) {
    Serial.println("[" + getTimestamp() + "] BLE Provisioning: READY");
    if (bleImageService.begin(wifiAP.getBLEServer(), wifiAP.getDeviceID())) {
      Serial.println("[" + getTimestamp() + "] BLE Image Service: READY");
    } else {
      Serial.println("[" + getTimestamp() + "] WARNING: BLE Image Service initialization failed");
    }
  } else {
    Serial.println("[" + getTimestamp() + "] WARNING: BLE initialization failed");
  }

  // Initialize camera module
  if (initCamera()) {
    Serial.println("[" + getTimestamp() + "] Camera module: READY");
  } else {
    Serial.println("[" + getTimestamp() + "] WARNING: Camera initialization failed");
  }

  // Setup web server routes
  webServer.setupRoutes();

  // Start web server
  server.begin();
  Serial.println("[" + getTimestamp() + "] Web server: READY");

  Serial.println("\n========================================");
  Serial.println("WiFi Credentials (also available via BLE):");
  Serial.println("  SSID: " + wifiAP.getSSID());
  Serial.println("  Password: " + wifiAP.getPassword());
  Serial.println("  Device ID: " + wifiAP.getDeviceID());
  Serial.println("\nBLE Device Name: CyberGlass-" + wifiAP.getDeviceID());
  Serial.println("Use a BLE scanner app to read credentials wirelessly");
  Serial.println("\nWeb Interface: http://" + wifiAP.getAPIP().toString());
  Serial.println("Type 'help' or 'h' for serial camera commands");
  Serial.println("========================================\n");
}

void loop() {
  // Process camera commands from serial port
  processCameraCommand();
  // Handle BLE image capture requests
  bleImageService.loop();
  // Small delay to prevent excessive CPU usage
  delay(10);
}
