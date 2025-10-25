#include "wifi_provisioning.h"

// BLE Callback class for handling characteristic writes
class WiFiProvisioningCallbacks : public BLECharacteristicCallbacks {
private:
    WiFiProvisioning* provisioning;

public:
    WiFiProvisioningCallbacks(WiFiProvisioning* prov) : provisioning(prov) {}

    void onWrite(BLECharacteristic* pCharacteristic) {
        std::string uuid = pCharacteristic->getUUID().toString();
        std::string value = pCharacteristic->getValue();

        // Debug: print raw bytes received
        Serial.printf("BLE Write - UUID: %s, Length: %d bytes\n", uuid.c_str(), value.length());
        Serial.print("Raw data (hex): ");
        for (size_t i = 0; i < value.length(); i++) {
            Serial.printf("%02X ", (uint8_t)value[i]);
        }
        Serial.println();

        if (uuid == BLE_CHAR_EXT_SSID_UUID) {
            // Use string constructor with length to handle all characters including nulls
            provisioning->externalSSID = String(value.c_str(), value.length());
            Serial.println("Received external SSID via BLE: '" + provisioning->externalSSID + "'");
            Serial.printf("SSID bytes received: %d, String length: %d\n", value.length(), provisioning->externalSSID.length());
            provisioning->saveExternalCredentials();
        }
        else if (uuid == BLE_CHAR_EXT_PASSWORD_UUID) {
            // Use string constructor with length to handle all characters including nulls
            provisioning->externalPassword = String(value.c_str(), value.length());
            Serial.println("Received external WiFi password via BLE");
            Serial.printf("Password bytes received: %d, String length: %d\n", value.length(), provisioning->externalPassword.length());
            provisioning->saveExternalCredentials();

            // Auto-connect to external WiFi if both SSID and password are set
            if (provisioning->externalSSID.length() > 0) {
                Serial.println("Attempting to connect to external WiFi...");
                provisioning->connectToExternalWiFi();
            }
        }
        else if (uuid == BLE_CHAR_WIFI_MODE_UUID) {
            if (value.length() > 0) {
                int mode = (int)value[0];
                provisioning->setWiFiMode(mode);
                Serial.printf("WiFi mode changed to: %d\n", mode);
            }
        }
    }
};

WiFiProvisioning::WiFiProvisioning() : apRunning(false), staConnected(false),
                                        wifiMode(WIFI_MODE_AP_ONLY),
                                        pServer(nullptr), pCharSSID(nullptr), pCharPassword(nullptr),
                                        pCharExtSSID(nullptr), pCharExtPassword(nullptr),
                                        pCharWiFiStatus(nullptr), pCharSTAIP(nullptr), pCharWiFiMode(nullptr) {
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

    // Load WiFi mode
    wifiMode = preferences.getInt("wifi_mode", WIFI_MODE_AP_ONLY);

    preferences.end();
}

void WiFiProvisioning::loadExternalCredentials() {
    preferences.begin("cyberglass", false);

    externalSSID = preferences.getString("ext_ssid", "");
    externalPassword = preferences.getString("ext_password", "");

    if (externalSSID.length() > 0) {
        Serial.println("Loaded external WiFi credentials from NVS");
        Serial.println("External SSID: " + externalSSID);
    }

    preferences.end();
}

void WiFiProvisioning::saveExternalCredentials() {
    preferences.begin("cyberglass", false);

    if (externalSSID.length() > 0) {
        preferences.putString("ext_ssid", externalSSID);
    }
    if (externalPassword.length() > 0) {
        preferences.putString("ext_password", externalPassword);
    }

    preferences.end();
    Serial.println("Saved external WiFi credentials to NVS");
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

    Serial.println("BLE initialized (no pairing required)");
    Serial.println("Security will be enforced via WiFi network and web interface");

    // Create BLE Server
    pServer = BLEDevice::createServer();

    // Create BLE Service
    BLEService *pService = pServer->createService(BLE_SERVICE_UUID);

    // Create SSID Characteristic (Read) - AP credentials
    pCharSSID = pService->createCharacteristic(
        BLE_CHAR_SSID_UUID,
        BLECharacteristic::PROPERTY_READ
    );
    pCharSSID->setValue(ssid.c_str());

    // Create Password Characteristic (Read) - AP password
    pCharPassword = pService->createCharacteristic(
        BLE_CHAR_PASSWORD_UUID,
        BLECharacteristic::PROPERTY_READ
    );
    pCharPassword->setValue(password.c_str());

    // Create External SSID Characteristic (Write) - For sending external WiFi SSID
    pCharExtSSID = pService->createCharacteristic(
        BLE_CHAR_EXT_SSID_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pCharExtSSID->setCallbacks(new WiFiProvisioningCallbacks(this));

    // Create External Password Characteristic (Write) - For sending external WiFi password
    pCharExtPassword = pService->createCharacteristic(
        BLE_CHAR_EXT_PASSWORD_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pCharExtPassword->setCallbacks(new WiFiProvisioningCallbacks(this));

    // Create WiFi Status Characteristic (Read/Notify) - Connection status
    pCharWiFiStatus = pService->createCharacteristic(
        BLE_CHAR_WIFI_STATUS_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharWiFiStatus->addDescriptor(new BLE2902());
    pCharWiFiStatus->setValue("Disconnected");

    // Create STA IP Characteristic (Read/Notify) - IP address in Station mode
    pCharSTAIP = pService->createCharacteristic(
        BLE_CHAR_STA_IP_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharSTAIP->addDescriptor(new BLE2902());
    pCharSTAIP->setValue("0.0.0.0");

    // Create WiFi Mode Characteristic (Write) - Set WiFi mode
    pCharWiFiMode = pService->createCharacteristic(
        BLE_CHAR_WIFI_MODE_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pCharWiFiMode->setCallbacks(new WiFiProvisioningCallbacks(this));

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
    Serial.println("\n=== WiFi Status ===");
    Serial.printf("WiFi Mode: %d (0=AP, 1=STA, 2=AP+STA)\n", wifiMode);

    if (apRunning) {
        Serial.println("\n--- Access Point ---");
        Serial.print("Status: Running\n");
        Serial.print("SSID: ");
        Serial.println(ssid);
        Serial.print("Password: ");
        Serial.println(password);
        Serial.print("IP Address: ");
        Serial.println(apIP);
        Serial.print("Connected Clients: ");
        Serial.println(getClientCount());
    }

    if (staConnected) {
        Serial.println("\n--- Station Mode ---");
        Serial.println("Status: Connected");
        Serial.print("External SSID: ");
        Serial.println(externalSSID);
        Serial.print("IP Address: ");
        Serial.println(staIP);
        Serial.print("mDNS Hostname: ");
        Serial.println(getMDNSHostname());
    } else if (externalSSID.length() > 0) {
        Serial.println("\n--- Station Mode ---");
        Serial.println("Status: Disconnected");
        Serial.print("External SSID: ");
        Serial.println(externalSSID);
    }

    Serial.println("======================\n");
}

// Station Mode Implementation
bool WiFiProvisioning::initSTA(String extSSID, String extPassword) {
    Serial.println("=== Initializing WiFi Station Mode ===");

    externalSSID = extSSID;
    externalPassword = extPassword;

    // Save credentials to NVS
    saveExternalCredentials();

    // Attempt to connect
    return connectToExternalWiFi();
}

bool WiFiProvisioning::connectToExternalWiFi() {
    if (externalSSID.length() == 0) {
        Serial.println("No external WiFi credentials configured");
        return false;
    }

    Serial.println("======================================");
    Serial.println("Connecting to external WiFi");
    Serial.print("SSID: ");
    Serial.println(externalSSID);
    Serial.printf("SSID Length: %d bytes\n", externalSSID.length());
    Serial.printf("Password Length: %d bytes\n", externalPassword.length());
    Serial.println("======================================");

    // Update BLE status characteristic
    if (pCharWiFiStatus) {
        pCharWiFiStatus->setValue("Connecting...");
        pCharWiFiStatus->notify();
    }

    // Disconnect any existing connection
    WiFi.disconnect(true);
    delay(100);

    // Set WiFi mode based on current mode setting
    if (wifiMode == WIFI_MODE_STA_ONLY) {
        Serial.println("Setting WiFi mode to STA");
        WiFi.mode(WIFI_STA);
    } else if (wifiMode == WIFI_MODE_AP_STA) {
        Serial.println("Setting WiFi mode to AP+STA");
        WiFi.mode(WIFI_AP_STA);
    }

    delay(100);

    // Attempt connection with retry
    for (int attempt = 1; attempt <= STA_RETRY_ATTEMPTS; attempt++) {
        Serial.printf("\n=== Attempt %d/%d ===\n", attempt, STA_RETRY_ATTEMPTS);

        WiFi.begin(externalSSID.c_str(), externalPassword.c_str());

        unsigned long startTime = millis();
        wl_status_t status = WiFi.status();

        while (status != WL_CONNECTED && (millis() - startTime) < STA_CONNECT_TIMEOUT) {
            delay(500);
            status = WiFi.status();
            Serial.print(".");

            // Print status every 2 seconds
            if (((millis() - startTime) / 1000) % 2 == 0) {
                Serial.printf(" [Status: %d] ", status);
            }
        }
        Serial.println();

        status = WiFi.status();
        Serial.printf("Final status: %d (", status);

        switch(status) {
            case WL_CONNECTED:
                Serial.print("CONNECTED");
                break;
            case WL_NO_SSID_AVAIL:
                Serial.print("SSID NOT FOUND");
                break;
            case WL_CONNECT_FAILED:
                Serial.print("WRONG PASSWORD");
                break;
            case WL_IDLE_STATUS:
                Serial.print("IDLE");
                break;
            case WL_DISCONNECTED:
                Serial.print("DISCONNECTED");
                break;
            default:
                Serial.print("UNKNOWN");
                break;
        }
        Serial.println(")");

        if (status == WL_CONNECTED) {
            staConnected = true;
            staIP = WiFi.localIP();

            Serial.println("\n*** SUCCESS! Connected to external WiFi! ***");
            Serial.print("IP Address: ");
            Serial.println(staIP);
            Serial.print("Gateway: ");
            Serial.println(WiFi.gatewayIP());
            Serial.print("Subnet: ");
            Serial.println(WiFi.subnetMask());
            Serial.print("DNS: ");
            Serial.println(WiFi.dnsIP());

            // Update BLE characteristics
            if (pCharWiFiStatus) {
                pCharWiFiStatus->setValue("Connected");
                pCharWiFiStatus->notify();
            }
            if (pCharSTAIP) {
                pCharSTAIP->setValue(staIP.toString().c_str());
                pCharSTAIP->notify();
            }

            // Start mDNS
            startMDNS();

            Serial.println("======================================");
            return true;
        }

        Serial.printf("*** Attempt %d failed ***\n", attempt);

        if (attempt < STA_RETRY_ATTEMPTS) {
            Serial.println("Waiting 2 seconds before retry...");
            delay(2000);
        }
    }

    // All attempts failed
    staConnected = false;
    Serial.println("Failed to connect to external WiFi after all attempts");

    // Update BLE status
    if (pCharWiFiStatus) {
        pCharWiFiStatus->setValue("Failed");
        pCharWiFiStatus->notify();
    }

    // Fallback to AP mode if we're in STA-only mode
    if (wifiMode == WIFI_MODE_STA_ONLY) {
        Serial.println("Falling back to AP mode...");
        wifiMode = WIFI_MODE_AP_ONLY;
        initAP();
    }

    Serial.println("======================================");
    return false;
}

bool WiFiProvisioning::isConnectedToSTA() {
    return staConnected && (WiFi.status() == WL_CONNECTED);
}

IPAddress WiFiProvisioning::getSTAIP() {
    return staIP;
}

String WiFiProvisioning::getSTAStatus() {
    if (staConnected) {
        return "Connected to " + externalSSID;
    } else if (externalSSID.length() > 0) {
        return "Disconnected (configured: " + externalSSID + ")";
    } else {
        return "Not configured";
    }
}

// Dual Mode (AP + STA)
bool WiFiProvisioning::initAPSTA() {
    Serial.println("=== Initializing Dual Mode (AP + STA) ===");

    wifiMode = WIFI_MODE_AP_STA;

    // Save mode to NVS
    preferences.begin("cyberglass", false);
    preferences.putInt("wifi_mode", wifiMode);
    preferences.end();

    // Load credentials
    loadOrGenerateCredentials();
    loadExternalCredentials();

    // Set WiFi to AP+STA mode
    WiFi.mode(WIFI_AP_STA);
    delay(100);

    // Start AP
    bool apSuccess = WiFi.softAP(ssid.c_str(), password.c_str(), AP_CHANNEL, 0, AP_MAX_CONNECTIONS);
    if (apSuccess) {
        apRunning = true;
        apIP = WiFi.softAPIP();
        Serial.println("AP started: " + ssid);
        Serial.println("AP IP: " + apIP.toString());
    }

    // Connect to external WiFi if configured
    if (externalSSID.length() > 0) {
        connectToExternalWiFi();
    }

    Serial.println("======================================");
    return apSuccess;
}

// WiFi Mode Management
void WiFiProvisioning::setWiFiMode(int mode) {
    if (mode < WIFI_MODE_AP_ONLY || mode > WIFI_MODE_AP_STA) {
        Serial.println("Invalid WiFi mode");
        return;
    }

    wifiMode = mode;

    // Save to NVS
    preferences.begin("cyberglass", false);
    preferences.putInt("wifi_mode", wifiMode);
    preferences.end();

    Serial.printf("WiFi mode set to: %d\n", wifiMode);
}

int WiFiProvisioning::getWiFiMode() {
    return wifiMode;
}

String WiFiProvisioning::getExternalSSID() {
    return externalSSID;
}

String WiFiProvisioning::getExternalPassword() {
    return externalPassword;
}

// mDNS Service Discovery
bool WiFiProvisioning::startMDNS() {
    String hostname = "cyberglass-" + deviceID;
    hostname.toLowerCase();

    if (MDNS.begin(hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("cyberglass", "tcp", 80);

        Serial.println("mDNS responder started");
        Serial.print("Hostname: ");
        Serial.print(hostname);
        Serial.println(".local");
        Serial.print("Access device at: http://");
        Serial.print(hostname);
        Serial.println(".local");

        return true;
    } else {
        Serial.println("Error starting mDNS");
        return false;
    }
}

String WiFiProvisioning::getMDNSHostname() {
    String hostname = "cyberglass-" + deviceID;
    hostname.toLowerCase();
    return hostname + ".local";
}
