#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "ble_image_transfer.h"
#include "camera_module.h"
#include "network_provisioning.h"
#include "web_server.h"

// Global instances
WiFiProvisioning wifiAP; // Using backward-compatible alias for NetworkProvisioning
AsyncWebServer server(80);
WebServerManager webServer(&server, &wifiAP);
BLEImageTransfer bleImageTransfer;

void setup() {
  Serial.begin(115200); // Initialize serial communication
  delay(1000);

  // Initialize system
  Serial.println("\n========================================");
  Serial.println("  XIAO ESP32S3 - CyberGlass System");
  Serial.println("========================================");

  // Initialize WiFi (AP mode by default, or AP+STA if external WiFi is configured)
  // Load any saved external WiFi credentials first
  wifiAP.loadExternalCredentials();

  // Check if external WiFi is configured and start in appropriate mode
  bool wifiConnected = false;
  if (wifiAP.getExternalSSID().length() > 0 && wifiAP.getWiFiMode() != WIFI_MODE_AP_ONLY) {
    // Start in dual mode (AP + STA)
    Serial.println("External WiFi configured, starting in AP+STA mode...");
    if (wifiAP.initAPSTA()) {
      Serial.println("[" + getTimestamp() + "] WiFi AP+STA: READY");
      wifiConnected = wifiAP.isConnectedToSTA();
    } else {
      Serial.println("[" + getTimestamp() + "] WARNING: WiFi initialization failed");
    }
  } else {
    // Start in AP-only mode (default)
    if (wifiAP.initAP()) {
      Serial.println("[" + getTimestamp() + "] WiFi AP: READY");
    } else {
      Serial.println("[" + getTimestamp() + "] WARNING: WiFi AP initialization failed");
    }
  }

  // Initialize BLE only if WiFi STA is NOT connected (save resources)
  if (!wifiConnected) {
    // Initialize BLE with image transfer support (image transfer characteristics added before service starts)
    if (wifiAP.initBLE(&bleImageTransfer)) {
      Serial.println("[" + getTimestamp() + "] BLE Provisioning: READY (for WiFi setup and image transfer)");
    } else {
      Serial.println("[" + getTimestamp() + "] WARNING: BLE initialization failed");
    }
  } else {
    Serial.println("[" + getTimestamp() + "] BLE: DISABLED (WiFi connected, saving resources)");
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

  // Start mDNS for easy discovery
  if (wifiAP.startMDNS()) {
    Serial.println("[" + getTimestamp() + "] MDNS started");
  } else {
    Serial.println("[" + getTimestamp() + "] WARNING: MDNS initialization failed");
  }

  Serial.println("[" + getTimestamp() + "] mDNS: READY");

  // Print connection information
  Serial.println("\n========================================");
  Serial.println("WiFi Credentials (also available via BLE):");
  Serial.println("  AP SSID: " + wifiAP.getSSID());
  Serial.println("  AP Password: " + wifiAP.getPassword());
  Serial.println("  Device ID: " + wifiAP.getDeviceID());

  if (wifiAP.isConnectedToSTA()) {
    Serial.println("\nStation Mode:");
    Serial.println("  Status: Connected to " + wifiAP.getExternalSSID());
    Serial.println("  STA IP: " + wifiAP.getSTAIP().toString());
    Serial.println("  mDNS: http://" + wifiAP.getMDNSHostname());
  }

  Serial.println("\nBLE Device Name: CyberGlass-" + wifiAP.getDeviceID());
  Serial.println("Use a BLE scanner app to:");
  Serial.println("  - Read AP credentials wirelessly");
  Serial.println("  - Send external WiFi credentials");
  Serial.println("  - Switch WiFi modes");
  Serial.println("  - Request and transfer images via BLE");
  Serial.println("\nNote: BLE is open for initial setup (within range ~10m)");
  Serial.println("Security enforced via WiFi network isolation");

  Serial.println("\nAPI Endpoints:");
  Serial.println("  AP Mode: http://" + wifiAP.getAPIP().toString());
  if (wifiAP.isConnectedToSTA()) {
    Serial.println("  STA Mode: http://" + wifiAP.getSTAIP().toString());
    Serial.println("  mDNS: http://" + wifiAP.getMDNSHostname());
  }

  Serial.println("========================================\n");
}

void loop() {
  // Yield to WiFi tasks
  yield();
}
