#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// WiFi AP Configuration
#define AP_SSID_PREFIX "CyberGlass-"
#define AP_CHANNEL 1
#define AP_MAX_CONNECTIONS 4

// BLE Configuration
#define BLE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_SSID_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_PASSWORD_UUID  "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"
#define BLE_DEVICE_NAME_PREFIX  "CyberGlass-"

// Web Server Configuration
#define WEB_SERVER_PORT 80

// WiFi Provisioning Class
class WiFiProvisioning {
public:
    WiFiProvisioning();

    // Initialize WiFi as Access Point with unique SSID and password
    bool initAP();

    // Initialize BLE for provisioning
    bool initBLE();

    // Stop BLE (after provisioning complete)
    void stopBLE();

    // Get AP IP address
    IPAddress getAPIP();

    // Get number of connected clients
    int getClientCount();

    // Check if WiFi is running
    bool isAPRunning();

    // Get SSID
    String getSSID();

    // Get Password
    String getPassword();

    // Print WiFi status
    void printStatus();

    // Generate device ID from MAC address
    String getDeviceID();

private:
    bool apRunning;
    IPAddress apIP;
    String ssid;
    String password;
    String deviceID;
    Preferences preferences;
    BLEServer* pServer;
    BLECharacteristic* pCharSSID;
    BLECharacteristic* pCharPassword;

    // Generate random password
    String generatePassword();

    // Load or generate credentials
    void loadOrGenerateCredentials();
};

#endif // WIFI_PROVISIONING_H
