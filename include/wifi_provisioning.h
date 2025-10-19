#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// WiFi AP Configuration
#define AP_SSID "CyberGlass-AP"
#define AP_PASSWORD "cyberglass123"  // Minimum 8 characters for WPA2
#define AP_CHANNEL 1
#define AP_MAX_CONNECTIONS 4

// Web Server Configuration
#define WEB_SERVER_PORT 80

// WiFi Provisioning Class
class WiFiProvisioning {
public:
    WiFiProvisioning();

    // Initialize WiFi as Access Point
    bool initAP();

    // Get AP IP address
    IPAddress getAPIP();

    // Get number of connected clients
    int getClientCount();

    // Check if WiFi is running
    bool isAPRunning();

    // Get SSID
    String getSSID();

    // Print WiFi status
    void printStatus();

private:
    bool apRunning;
    IPAddress apIP;
};

#endif // WIFI_PROVISIONING_H
