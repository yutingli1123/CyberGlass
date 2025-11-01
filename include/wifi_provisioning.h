#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

// WiFi AP Configuration
#define AP_SSID_PREFIX "CyberGlass-"
#define AP_CHANNEL 1
#define AP_MAX_CONNECTIONS 4

// BLE Configuration
#define BLE_SERVICE_UUID "c153f40c-b1eb-11f0-bf8b-dbb9b632dc78"
#define BLE_CHAR_SSID_UUID "c5dad860-b1eb-11f0-96db-bb1907c420ec"
#define BLE_CHAR_PASSWORD_UUID "c8ee211a-b1eb-11f0-bfc4-777d4dda4cde"
#define BLE_DEVICE_NAME_PREFIX "CyberGlass-"

// New BLE Characteristics for External WiFi Provisioning
#define BLE_CHAR_EXT_SSID_UUID "e2bd3d24-b1eb-11f0-8222-abd7b78f2ca6" // WRITE - External WiFi SSID
#define BLE_CHAR_EXT_PASSWORD_UUID "e616b554-b1eb-11f0-bdab-fb872f685991" // WRITE - External WiFi Password
#define BLE_CHAR_WIFI_STATUS_UUID "e9855d30-b1eb-11f0-96aa-abcc2336cb9a" // READ/NOTIFY - Connection status
#define BLE_CHAR_STA_IP_UUID "f07cce0c-b1eb-11f0-b066-372eb4121903" // READ/NOTIFY - STA mode IP address
#define BLE_CHAR_WIFI_MODE_UUID "f456ce38-b1eb-11f0-a60f-2b366feccc96" // WRITE - WiFi mode (0=AP, 1=STA, 2=AP+STA)

// WiFi Mode Constants
#define WIFI_MODE_AP_ONLY 0
#define WIFI_MODE_STA_ONLY 1
#define WIFI_MODE_AP_STA 2

// Connection timeout and retry
#define STA_CONNECT_TIMEOUT 10000 // 10 seconds
#define STA_RETRY_ATTEMPTS 3

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
  bool initSTA(const String &extSSID, const String &extPassword);
  bool connectToExternalWiFi();
  bool isConnectedToSTA() const;
  IPAddress getSTAIP();
  String getSTAStatus() const; // Returns connection status string

  // Dual Mode (AP + STA simultaneously)
  bool initAPSTA();

  // WiFi Mode Management
  void setWiFiMode(int mode); // 0=AP, 1=STA, 2=AP+STA
  int getWiFiMode() const;

  // mDNS Service Discovery
  bool startMDNS() const;
  String getMDNSHostname() const;

  // Get AP IP address
  IPAddress getAPIP();

  // Get number of connected clients
  int getClientCount() const;
  bool isAPRunning() const;

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
  void printStatus() const;

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
  int wifiMode; // 0=AP, 1=STA, 2=AP+STA
  Preferences preferences;

  // BLE Server and Characteristics
  BLEServer *pServer;
  BLECharacteristic *pCharSSID;
  BLECharacteristic *pCharPassword;
  BLECharacteristic *pCharExtSSID;
  BLECharacteristic *pCharExtPassword;
  BLECharacteristic *pCharWiFiStatus;
  BLECharacteristic *pCharSTAIP;
  BLECharacteristic *pCharWiFiMode;

  // BLE Callbacks (stored to prevent memory leak)
  BLEServerCallbacks *pServerCallbacks;
  BLECharacteristicCallbacks *pExtSSIDCallbacks;
  BLECharacteristicCallbacks *pExtPasswordCallbacks;
  BLECharacteristicCallbacks *pWiFiModeCallbacks;

  // Generate random password
  static String generatePassword();

  // Load or generate credentials
  void loadOrGenerateCredentials();

  // Save external WiFi credentials to NVS
  void saveExternalCredentials();

  // BLE Callbacks for handling writes
  friend class WiFiProvisioningCallbacks;
};

#endif // WIFI_PROVISIONING_H
