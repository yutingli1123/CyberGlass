#include "wifi_provisioning.h"

WiFiProvisioning::WiFiProvisioning() : apRunning(false) {
}

bool WiFiProvisioning::initAP() {
    Serial.println("=== Initializing WiFi Access Point ===");

    // Disconnect from any previous WiFi connection
    WiFi.disconnect(true);
    delay(100);

    // Set WiFi mode to Access Point
    WiFi.mode(WIFI_AP);
    delay(100);

    // Configure and start Access Point
    bool success = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONNECTIONS);

    if (success) {
        apIP = WiFi.softAPIP();
        apRunning = true;

        Serial.println("WiFi AP started successfully!");
        Serial.print("SSID: ");
        Serial.println(AP_SSID);
        Serial.print("Password: ");
        Serial.println(AP_PASSWORD);
        Serial.print("IP Address: ");
        Serial.println(apIP);
        Serial.print("Max Connections: ");
        Serial.println(AP_MAX_CONNECTIONS);
        Serial.println("======================================");

        return true;
    } else {
        Serial.println("Failed to start WiFi AP!");
        apRunning = false;
        return false;
    }
}

IPAddress WiFiProvisioning::getAPIP() {
    return apIP;
}

int WiFiProvisioning::getClientCount() {
    if (!apRunning) return 0;
    return WiFi.softAPgetStationNum();
}

bool WiFiProvisioning::isAPRunning() {
    return apRunning;
}

String WiFiProvisioning::getSSID() {
    return String(AP_SSID);
}

void WiFiProvisioning::printStatus() {
    if (apRunning) {
        Serial.println("\n=== WiFi AP Status ===");
        Serial.print("Status: Running\n");
        Serial.print("SSID: ");
        Serial.println(AP_SSID);
        Serial.print("IP Address: ");
        Serial.println(apIP);
        Serial.print("Connected Clients: ");
        Serial.println(getClientCount());
        Serial.println("======================\n");
    } else {
        Serial.println("WiFi AP is not running.");
    }
}
