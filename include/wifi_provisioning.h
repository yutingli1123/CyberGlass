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
#include <BLESecurity.h>
#include <ESPmDNS.h>

// WiFi AP Configuration
#define AP_SSID_PREFIX "CyberGlass-"
#define AP_CHANNEL 1
#define AP_MAX_CONNECTIONS 4

// BLE Configuration
#define BLE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_SSID_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_PASSWORD_UUID  "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"
#define BLE_DEVICE_NAME_PREFIX  "CyberGlass-"

// New BLE Characteristics for External WiFi Provisioning
#define BLE_CHAR_EXT_SSID_UUID      "a3c87500-8ed3-4bdf-8a39-a01bebede295"     // WRITE - External WiFi SSID
#define BLE_CHAR_EXT_PASSWORD_UUID  "a3c87501-8ed3-4bdf-8a39-a01bebede295"     // WRITE - External WiFi Password
#define BLE_CHAR_WIFI_STATUS_UUID   "a3c87502-8ed3-4bdf-8a39-a01bebede295"     // READ/NOTIFY - Connection status
#define BLE_CHAR_STA_IP_UUID        "a3c87503-8ed3-4bdf-8a39-a01bebede295"     // READ/NOTIFY - STA mode IP address
#define BLE_CHAR_WIFI_MODE_UUID     "a3c87504-8ed3-4bdf-8a39-a01bebede295"     // WRITE - WiFi mode (0=AP, 1=STA, 2=AP+STA)

// WiFi Mode Constants
#define WIFI_MODE_AP_ONLY    0
#define WIFI_MODE_STA_ONLY   1
#define WIFI_MODE_AP_STA     2

// Connection timeout and retry
#define STA_CONNECT_TIMEOUT  10000  // 10 seconds
#define STA_RETRY_ATTEMPTS   3

// Web Server Configuration
#define WEB_SERVER_PORT 80

// WiFi Provisioning Class
class WiFiProvisioning {
public:
    WiFiProvisioning();

    // Initialize WiFi as Access Point with unique SSID and password
    bool initAP();

    // Initialize BLE for provisioning (with pairing security)
    bool initBLE();

    // Stop BLE (after provisioning complete)
    void stopBLE();

    // Station Mode (Connect to external WiFi)
    bool initSTA(String extSSID, String extPassword);
    bool connectToExternalWiFi();
    bool isConnectedToSTA();
    IPAddress getSTAIP();
    String getSTAStatus();  // Returns connection status string

    // Dual Mode (AP + STA simultaneously)
    bool initAPSTA();

    // WiFi Mode Management
    void setWiFiMode(int mode);  // 0=AP, 1=STA, 2=AP+STA
    int getWiFiMode();

    // mDNS Service Discovery
    bool startMDNS();
    String getMDNSHostname();

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

    // Get external WiFi credentials
    String getExternalSSID();
    String getExternalPassword();

    // Load external WiFi credentials from NVS (public for use in main.cpp)
    void loadExternalCredentials();

    // Print WiFi status
    void printStatus();

    // Generate device ID from MAC address
    String getDeviceID();

private:
    bool apRunning;
    bool staConnected;
    IPAddress apIP;
    IPAddress staIP;
    String ssid;
    String password;
    String externalSSID;
    String externalPassword;
    String deviceID;
    int wifiMode;  // 0=AP, 1=STA, 2=AP+STA
    Preferences preferences;

    // BLE Server and Characteristics
    BLEServer* pServer;
    BLECharacteristic* pCharSSID;
    BLECharacteristic* pCharPassword;
    BLECharacteristic* pCharExtSSID;
    BLECharacteristic* pCharExtPassword;
    BLECharacteristic* pCharWiFiStatus;
    BLECharacteristic* pCharSTAIP;
    BLECharacteristic* pCharWiFiMode;

    // Generate random password
    String generatePassword();

    // Load or generate credentials
    void loadOrGenerateCredentials();

    // Save external WiFi credentials to NVS
    void saveExternalCredentials();

    // BLE Callbacks for handling writes
    friend class WiFiProvisioningCallbacks;
};

#endif // WIFI_PROVISIONING_H
