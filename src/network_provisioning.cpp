#include "network_provisioning.h"
#include "ble_image_transfer.h"

// BLE Server Callbacks for handling connections/disconnections
class CyberGlassBLEServerCallbacks final : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override { Serial.println("BLE: Client connected"); }

  void onDisconnect(BLEServer *pServer) override {
    Serial.println("BLE: Client disconnected");
    // Restart advertising so other devices can connect
    delay(500); // Give some time for cleanup
    BLEDevice::startAdvertising();
    Serial.println("BLE: Advertising restarted");
  }
};

// BLE Callback class for handling characteristic writes
class WiFiProvisioningCallbacks final : public BLECharacteristicCallbacks {
  NetworkProvisioning *provisioning;

public:
  explicit WiFiProvisioningCallbacks(NetworkProvisioning *prov) : provisioning(prov) {}

  void onWrite(BLECharacteristic *pCharacteristic) override {
    const std::string uuid = pCharacteristic->getUUID().toString();
    const std::string value = pCharacteristic->getValue();

    // Debug: print raw bytes received
    Serial.printf("BLE Write - UUID: %s, Length: %d bytes\n", uuid.c_str(), value.length());
    Serial.print("Raw data (hex): ");
    for (size_t i = 0; i < value.length(); i++) {
      Serial.printf("%02X ", static_cast<uint8_t>(value[i]));
    }
    Serial.println();

    if (uuid == BLE_CHAR_EXT_SSID_UUID) {
      // Use string constructor with length to handle all characters including nulls
      provisioning->externalSSID = String(value.c_str(), value.length());
      Serial.println("Received external SSID via BLE: '" + provisioning->externalSSID + "'");
      Serial.printf("SSID bytes received: %d, String length: %d\n", value.length(),
                    provisioning->externalSSID.length());
      provisioning->saveExternalCredentials();
    } else if (uuid == BLE_CHAR_EXT_PASSWORD_UUID) {
      // Use string constructor with length to handle all characters including nulls
      provisioning->externalPassword = String(value.c_str(), value.length());
      Serial.println("Received external WiFi password via BLE");
      Serial.printf("Password bytes received: %d, String length: %d\n", value.length(),
                    provisioning->externalPassword.length());
      provisioning->saveExternalCredentials();

      // Auto-connect to external Wi-Fi if both SSID and password are set
      if (provisioning->externalSSID.length() > 0) {
        Serial.println("Attempting to connect to external WiFi...");
        provisioning->connectToExternalWiFi();
      }
    } else if (uuid == BLE_CHAR_WIFI_MODE_UUID) {
      if (!value.empty()) {
        const int mode = value[0];
        provisioning->setWiFiMode(mode);
        Serial.printf("WiFi mode changed to: %d\n", mode);
      }
    }
  }
};

NetworkProvisioning::NetworkProvisioning() :
    apRunning(false), staConnected(false), wifiMode(WIFI_MODE_AP_ONLY), pServer(nullptr), pService(nullptr),
    pCharSSID(nullptr), pCharPassword(nullptr), pCharExtSSID(nullptr), pCharExtPassword(nullptr),
    pCharWiFiStatus(nullptr), pCharSTAIP(nullptr), pCharWiFiMode(nullptr), pServerCallbacks(nullptr),
    pExtSSIDCallbacks(nullptr), pExtPasswordCallbacks(nullptr), pWiFiModeCallbacks(nullptr) {
  // Generate device ID from MAC address
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char macStr[5];
  sprintf(macStr, "%02X%02X", mac[4], mac[5]);
  deviceID = String(macStr);
}

String NetworkProvisioning::generatePassword() {
  // Generate 12-character random password
  constexpr char charset[] = "abcdefghjkmnpqrstuvwxyz23456789"; // Excluding confusing chars
  String pwd = "";

  for (int i = 0; i < 12; i++) {
    pwd += charset[random(0, static_cast<long>(strlen(charset)))];
  }

  return pwd;
}

void NetworkProvisioning::loadOrGenerateCredentials() {
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

void NetworkProvisioning::loadExternalCredentials() {
  preferences.begin("cyberglass", false);

  externalSSID = preferences.getString("ext_ssid", "");
  externalPassword = preferences.getString("ext_password", "");

  if (externalSSID.length() > 0) {
    Serial.println("Loaded external WiFi credentials from NVS");
    Serial.println("External SSID: " + externalSSID);
  }

  preferences.end();
}

void NetworkProvisioning::saveExternalCredentials() {
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

bool NetworkProvisioning::initAP() {
  Serial.println("=== Initializing WiFi Access Point ===");

  // Load or generate credentials
  loadOrGenerateCredentials();

  // Disconnect from any previous Wi-Fi connection
  WiFi.disconnect(true);
  delay(100);

  // Set Wi-Fi mode to Access Point
  WiFiClass::mode(WIFI_AP);
  delay(100);

  // Configure and start Access Point
  const bool success = WiFi.softAP(ssid.c_str(), password.c_str(), AP_CHANNEL, 0, AP_MAX_CONNECTIONS);

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
  }
  Serial.println("Failed to start WiFi AP!");
  apRunning = false;
  return false;
}

IPAddress NetworkProvisioning::getAPIP() { return apIP; }

int NetworkProvisioning::getClientCount() const {
  if (!apRunning)
    return 0;
  return WiFi.softAPgetStationNum();
}

bool NetworkProvisioning::isAPRunning() const { return apRunning; }

String NetworkProvisioning::getSSID() { return ssid; }

String NetworkProvisioning::getPassword() { return password; }

String NetworkProvisioning::getDeviceID() { return deviceID; }

bool NetworkProvisioning::initBLE(BLEImageTransfer *imageTransfer) {
  Serial.println("=== Initializing BLE Provisioning ===");

  const String bleName = String(BLE_DEVICE_NAME_PREFIX) + deviceID;

  // Initialize BLE
  BLEDevice::init(bleName.c_str());

  Serial.println("BLE initialized (no pairing required)");
  Serial.println("Security will be enforced via WiFi network and web interface");

  // Create BLE Server
  pServer = BLEDevice::createServer();

  // Set server callbacks for connection/disconnection events
  pServerCallbacks = new CyberGlassBLEServerCallbacks();
  pServer->setCallbacks(pServerCallbacks);

  // Create BLE Service
  pService = pServer->createService(BLE_SERVICE_UUID);

  // Create SSID Characteristic (Read) - AP credentials
  pCharSSID = pService->createCharacteristic(BLE_CHAR_SSID_UUID, BLECharacteristic::PROPERTY_READ);
  pCharSSID->setValue(ssid.c_str());

  // Create Password Characteristic (Read) - AP password
  pCharPassword = pService->createCharacteristic(BLE_CHAR_PASSWORD_UUID, BLECharacteristic::PROPERTY_READ);
  pCharPassword->setValue(password.c_str());

  // Create External SSID Characteristic (Write) - For sending external Wi-Fi SSID
  pCharExtSSID = pService->createCharacteristic(BLE_CHAR_EXT_SSID_UUID, BLECharacteristic::PROPERTY_WRITE);
  pExtSSIDCallbacks = new WiFiProvisioningCallbacks(this);
  pCharExtSSID->setCallbacks(pExtSSIDCallbacks);

  // Create External Password Characteristic (Write) - For sending external Wi-Fi password
  pCharExtPassword = pService->createCharacteristic(BLE_CHAR_EXT_PASSWORD_UUID, BLECharacteristic::PROPERTY_WRITE);
  pExtPasswordCallbacks = new WiFiProvisioningCallbacks(this);
  pCharExtPassword->setCallbacks(pExtPasswordCallbacks);

  // Create Wi-Fi Status Characteristic (Read/Notify) - Connection status
  pCharWiFiStatus = pService->createCharacteristic(BLE_CHAR_WIFI_STATUS_UUID, BLECharacteristic::PROPERTY_READ |
                                                                                  BLECharacteristic::PROPERTY_NOTIFY);
  pCharWiFiStatus->addDescriptor(new BLE2902());
  pCharWiFiStatus->setValue("Disconnected");

  // Create STA IP Characteristic (Read/Notify) - IP address in Station mode
  pCharSTAIP = pService->createCharacteristic(BLE_CHAR_STA_IP_UUID,
                                              BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pCharSTAIP->addDescriptor(new BLE2902());
  pCharSTAIP->setValue("0.0.0.0");

  // Create Wi-Fi Mode Characteristic (Write) - Set WiFi mode
  pCharWiFiMode = pService->createCharacteristic(BLE_CHAR_WIFI_MODE_UUID, BLECharacteristic::PROPERTY_WRITE);
  pWiFiModeCallbacks = new WiFiProvisioningCallbacks(this);
  pCharWiFiMode->setCallbacks(pWiFiModeCallbacks);

  // Initialize BLE Image Transfer characteristics BEFORE starting service
  if (imageTransfer) {
    Serial.println("Adding BLE Image Transfer characteristics...");
    if (!imageTransfer->initCharacteristics(pServer, pService)) {
      Serial.println("WARNING: Failed to add image transfer characteristics");
    }
  }

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
  Serial.println("======================================");

  return true;
}

void NetworkProvisioning::stopBLE() {
  if (pServer) {
    // Clean up callback objects to prevent memory leak
    delete pServerCallbacks;
    delete pExtSSIDCallbacks;
    delete pExtPasswordCallbacks;
    delete pWiFiModeCallbacks;

    // Reset pointers
    pServerCallbacks = nullptr;
    pExtSSIDCallbacks = nullptr;
    pExtPasswordCallbacks = nullptr;
    pWiFiModeCallbacks = nullptr;

    // Deinitialize BLE
    BLEDevice::deinit(true);
    pServer = nullptr;
    pService = nullptr;
    Serial.println("BLE stopped and callbacks cleaned up");
  }
}

void NetworkProvisioning::printStatus() const {
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
bool NetworkProvisioning::initSTA(const String &extSSID, const String &extPassword) {
  Serial.println("=== Initializing WiFi Station Mode ===");

  externalSSID = extSSID;
  externalPassword = extPassword;

  // Save credentials to NVS
  saveExternalCredentials();

  // Attempt to connect
  return connectToExternalWiFi();
}

bool NetworkProvisioning::connectToExternalWiFi() {
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

  // Set Wi-Fi mode based on current mode setting
  if (wifiMode == WIFI_MODE_STA_ONLY) {
    Serial.println("Setting WiFi mode to STA");
    WiFiClass::mode(WIFI_STA);
  } else if (wifiMode == WIFI_MODE_AP_STA) {
    Serial.println("Setting WiFi mode to AP+STA");
    WiFiClass::mode(WIFI_AP_STA);
  }

  delay(100);

  // Attempt connection with retry
  for (int attempt = 1; attempt <= STA_RETRY_ATTEMPTS; attempt++) {
    Serial.printf("\n=== Attempt %d/%d ===\n", attempt, STA_RETRY_ATTEMPTS);

    WiFi.begin(externalSSID.c_str(), externalPassword.c_str());

    const unsigned long startTime = millis();
    wl_status_t status = WiFiClass::status();

    while (status != WL_CONNECTED && millis() - startTime < STA_CONNECT_TIMEOUT) {
      delay(500);
      status = WiFiClass::status();
      Serial.print(".");

      // Print status every 2 seconds
      if ((millis() - startTime) / 1000 % 2 == 0) {
        Serial.printf(" [Status: %d] ", status);
      }
    }
    Serial.println();

    status = WiFiClass::status();
    Serial.printf("Final status: %d (", status);

    switch (status) {
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
      if (startMDNS()) {
        Serial.println("MDNS started");
      } else {
        Serial.println("MDNS failed to start");
      }

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

bool NetworkProvisioning::isConnectedToSTA() const { return staConnected && WiFiClass::status() == WL_CONNECTED; }

IPAddress NetworkProvisioning::getSTAIP() { return staIP; }

String NetworkProvisioning::getSTAStatus() const {
  if (staConnected) {
    return "Connected to " + externalSSID;
  }
  if (externalSSID.length() > 0) {
    return "Disconnected (configured: " + externalSSID + ")";
  }
  return "Not configured";
}

// Dual Mode (AP + STA)
bool NetworkProvisioning::initAPSTA() {
  Serial.println("=== Initializing Dual Mode (AP + STA) ===");

  wifiMode = WIFI_MODE_AP_STA;

  // Save mode to NVS
  preferences.begin("cyberglass", false);
  preferences.putInt("wifi_mode", wifiMode);
  preferences.end();

  // Load credentials
  loadOrGenerateCredentials();
  loadExternalCredentials();

  // Set Wi-Fi to AP+STA mode
  WiFiClass::mode(WIFI_AP_STA);
  delay(100);

  // Start AP
  const bool apSuccess = WiFi.softAP(ssid.c_str(), password.c_str(), AP_CHANNEL, 0, AP_MAX_CONNECTIONS);
  if (apSuccess) {
    apRunning = true;
    apIP = WiFi.softAPIP();
    Serial.println("AP started: " + ssid);
    Serial.println("AP IP: " + apIP.toString());
  }

  // Connect to external Wi-Fi if configured
  if (externalSSID.length() > 0) {
    connectToExternalWiFi();
  }

  Serial.println("======================================");
  return apSuccess;
}

// WiFi Mode Management
void NetworkProvisioning::setWiFiMode(const int mode) {
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

int NetworkProvisioning::getWiFiMode() const { return wifiMode; }

String NetworkProvisioning::getExternalSSID() { return externalSSID; }

String NetworkProvisioning::getExternalPassword() { return externalPassword; }

// mDNS Service Discovery
bool NetworkProvisioning::startMDNS() const {
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
  }
  Serial.println("Error starting mDNS");
  return false;
}

String NetworkProvisioning::getMDNSHostname() const {
  String hostname = "cyberglass-" + deviceID;
  hostname.toLowerCase();
  return hostname + ".local";
}
