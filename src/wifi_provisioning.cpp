#include "wifi_provisioning.h"

WiFiProvisioning::WiFiProvisioning() : apRunning(false), pServer(nullptr),
                                        pCharSSID(nullptr), pCharPassword(nullptr) {
    // Generate device ID from MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macStr[5];
    sprintf(macStr, "%02X%02X", mac[4], mac[5]);
    deviceID = String(macStr);
}

String WiFiProvisioning::generatePassword() {
    // Generate 12-character random password
    const char charset[] = "abcdefghjkmnpqrstuvwxyz23456789"; // Excluding confusing chars
    String pwd = "";

    for (int i = 0; i < 12; i++) {
        pwd += charset[random(0, strlen(charset))];
    }

    return pwd;
}

void WiFiProvisioning::loadOrGenerateCredentials() {
    preferences.begin("cyberglass", false);

    // Check if credentials exist
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");

    if (ssid.length() == 0 || password.length() == 0) {
        // Generate new credentials
        ssid = String(AP_SSID_PREFIX) + deviceID;
        password = generatePassword();

        // Save to NVS
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);

        Serial.println("Generated new WiFi credentials");
    } else {
        Serial.println("Loaded existing WiFi credentials");
    }

    preferences.end();
}

bool WiFiProvisioning::initAP() {
    Serial.println("=== Initializing WiFi Access Point ===");

    // Load or generate credentials
    loadOrGenerateCredentials();

    // Disconnect from any previous WiFi connection
    WiFi.disconnect(true);
    delay(100);

    // Set WiFi mode to Access Point
    WiFi.mode(WIFI_AP);
    delay(100);

    // Configure and start Access Point
    bool success = WiFi.softAP(ssid.c_str(), password.c_str(), AP_CHANNEL, 0, AP_MAX_CONNECTIONS);

    if (success) {
        apIP = WiFi.softAPIP();
        apRunning = true;

        Serial.println("WiFi AP started successfully!");
        Serial.print("SSID: ");
        Serial.println(ssid);
        Serial.print("Password: ");
        Serial.println(password);
        Serial.print("Device ID: ");
        Serial.println(deviceID);
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
    return ssid;
}

String WiFiProvisioning::getPassword() {
    return password;
}

String WiFiProvisioning::getDeviceID() {
    return deviceID;
}

bool WiFiProvisioning::initBLE() {
    Serial.println("=== Initializing BLE Provisioning ===");

    String bleName = String(BLE_DEVICE_NAME_PREFIX) + deviceID;

    // Initialize BLE
    BLEDevice::init(bleName.c_str());

    // Create BLE Server
    pServer = BLEDevice::createServer();

    // Create BLE Service
    BLEService *pService = pServer->createService(BLE_SERVICE_UUID);

    // Create SSID Characteristic (Read)
    pCharSSID = pService->createCharacteristic(
        BLE_CHAR_SSID_UUID,
        BLECharacteristic::PROPERTY_READ
    );
    pCharSSID->setValue(ssid.c_str());

    // Create Password Characteristic (Read)
    pCharPassword = pService->createCharacteristic(
        BLE_CHAR_PASSWORD_UUID,
        BLECharacteristic::PROPERTY_READ
    );
    pCharPassword->setValue(password.c_str());

    // Start service
    pService->start();

    // Start advertising
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("BLE advertising started");
    Serial.print("BLE Device Name: ");
    Serial.println(bleName);
    Serial.println("Use BLE scanner app to connect and get credentials");
    Serial.println("======================================");

    return true;
}

void WiFiProvisioning::stopBLE() {
    if (pServer) {
        BLEDevice::deinit(true);
        pServer = nullptr;
        Serial.println("BLE stopped");
    }
}

void WiFiProvisioning::printStatus() {
    if (apRunning) {
        Serial.println("\n=== WiFi AP Status ===");
        Serial.print("Status: Running\n");
        Serial.print("SSID: ");
        Serial.println(ssid);
        Serial.print("Password: ");
        Serial.println(password);
        Serial.print("IP Address: ");
        Serial.println(apIP);
        Serial.print("Connected Clients: ");
        Serial.println(getClientCount());
        Serial.println("======================\n");
    } else {
        Serial.println("WiFi AP is not running.");
    }
}
