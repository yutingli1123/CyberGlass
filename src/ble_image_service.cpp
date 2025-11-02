#include "ble_image_service.h"
#include <BLEDevice.h>
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"

namespace {
constexpr size_t kMaxChunkBytes = 180;  // conservative max payload per notify
constexpr uint16_t kInvalidConnId = 0xFFFF;
}

BLEImageService::BLEImageService()
    : _server(nullptr),
      _service(nullptr),
      _controlCharacteristic(nullptr),
      _dataCharacteristic(nullptr),
      _controlDescriptor(nullptr),
      _dataDescriptor(nullptr),
      _captureRequested(false),
      _transferInProgress(false),
      _clientConnected(false),
      _lastTransferMs(0),
      _connectionId(kInvalidConnId) {}

bool BLEImageService::begin(BLEServer* server, const String& deviceLabel) {
  if (!server) {
    Serial.println("[" + getTimestamp() + "] ERROR: BLEImageService.begin called with null server");
    return false;
  }

  if (_service) {
    return true;  // already initialized
  }

  _server = server;
  _deviceLabel = deviceLabel;

  _service = _server->createService(BLE_IMAGE_SERVICE_UUID);
  if (!_service) {
    Serial.println("[" + getTimestamp() + "] ERROR: Failed to create BLE image service");
    return false;
  }

  _controlCharacteristic = _service->createCharacteristic(
      BLE_IMAGE_CONTROL_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_WRITE_NR |
          BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_INDICATE);
  if (!_controlCharacteristic) {
    Serial.println("[" + getTimestamp() + "] ERROR: Failed to create BLE image control characteristic");
    return false;
  }
  _controlCharacteristic->setCallbacks(new ControlCallbacks(this));
  _controlDescriptor = new BLE2902();
  _controlDescriptor->setNotifications(false);
  _controlDescriptor->setIndications(false);
  _controlCharacteristic->addDescriptor(_controlDescriptor);
  _controlCharacteristic->setValue("ready");

  _dataCharacteristic = _service->createCharacteristic(
      BLE_IMAGE_DATA_CHAR_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_INDICATE);
  if (!_dataCharacteristic) {
    Serial.println("[" + getTimestamp() + "] ERROR: Failed to create BLE image data characteristic");
    return false;
  }
  _dataDescriptor = new BLE2902();
  _dataDescriptor->setNotifications(false);
  _dataDescriptor->setIndications(false);
  _dataCharacteristic->addDescriptor(_dataDescriptor);

  _service->start();

  // Advertise new service UUID
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  if (advertising) {
    advertising->stop();
    delay(100);
    advertising->addServiceUUID(BLE_IMAGE_SERVICE_UUID);
  }

  // Update server callbacks to monitor connections
  _server->setCallbacks(new ServerCallbacks(this));

  if (advertising) {
    BLEDevice::startAdvertising();
  }

  notifyStatus("ready");
  Serial.println("[" + getTimestamp() + "] BLE image service initialized");
  return true;
}

void BLEImageService::loop() {
  if (_captureRequested && !_transferInProgress) {
    processCaptureRequest();
  }
}

void BLEImageService::stop() {
  _captureRequested = false;
  if (_transferInProgress) {
    notifyStatus("info:transfer_cancel_requested");
  }
}

bool BLEImageService::readyForTransfer() {
  if (!_clientConnected || !_dataDescriptor) {
    return false;
  }
  return _dataDescriptor->getNotifications() || _dataDescriptor->getIndications();
}

void BLEImageService::ControlCallbacks::onWrite(BLECharacteristic* characteristic) {
  if (!_parent) {
    return;
  }
  std::string value = characteristic->getValue();
  if (value.empty()) {
    return;
  }
  _parent->handleCommand(value);
}

void BLEImageService::ServerCallbacks::onConnect(BLEServer* pServer) {}

void BLEImageService::ServerCallbacks::onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
  if (_parent) {
    uint16_t conn_id = param ? param->connect.conn_id : kInvalidConnId;
    _parent->onClientConnected(conn_id);
  }
}

void BLEImageService::ServerCallbacks::onDisconnect(BLEServer* pServer) {
  if (_parent) {
    _parent->onClientDisconnected();
  }
  BLEDevice::startAdvertising();
}

void BLEImageService::ServerCallbacks::onDisconnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
  (void)param;
  onDisconnect(pServer);
}

void BLEImageService::handleCommand(const std::string& command) {
  String cmd = String(command.c_str());
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "capture" || cmd == "c") {
    if (!isCameraReady()) {
      notifyStatus("error:camera_not_ready");
      return;
    }
    if (!readyForTransfer()) {
      notifyStatus("error:not_subscribed");
      return;
    }
    if (_transferInProgress) {
      notifyStatus("busy:transfer_in_progress");
      return;
    }
    _captureRequested = true;
    notifyStatus("capture:queued");
  } else if (cmd == "status" || cmd == "s") {
    String status = "status:";
    status += (_clientConnected ? "connected" : "disconnected");
    status += ":";
    status += (readyForTransfer() ? "notify_on" : "notify_off");
    status += ":";
    status += (isCameraReady() ? "camera_ready" : "camera_off");
    status += ":";
    status += _deviceLabel;
    notifyStatus(status);
  } else if (cmd.startsWith("mtu")) {
    notifyStatus(String("info:mtu:") + BLEDevice::getMTU());
  } else {
    notifyStatus("error:unknown_command");
  }
}

void BLEImageService::processCaptureRequest() {
  _captureRequested = false;

  if (!readyForTransfer()) {
    notifyStatus("error:not_subscribed");
    return;
  }

  _transferInProgress = true;

  unsigned long captureStart = millis();
  camera_fb_t* fb = acquireFrameBuffer();
  if (!fb) {
    notifyStatus("error:capture_failed");
    _transferInProgress = false;
    return;
  }

  unsigned long captureDuration = millis() - captureStart;
  uint32_t photoIndex = incrementPhotoCount();

  String startPayload = "start:";
  startPayload += photoIndex;
  startPayload += ":";
  startPayload += fb->len;
  startPayload += ":";
  startPayload += fb->width;
  startPayload += ":";
  startPayload += fb->height;
  startPayload += ":";
  startPayload += captureDuration;
  notifyStatus(startPayload);

  size_t mtu = BLEDevice::getMTU();
  size_t maxPayload = (mtu > 3) ? mtu - 3 : 20;
  if (maxPayload > kMaxChunkBytes) {
    maxPayload = kMaxChunkBytes;
  }
  if (maxPayload < 20) {
    maxPayload = 20;
  }

  size_t sentBytes = 0;
  unsigned long transferStart = millis();
  bool aborted = false;
  const uint8_t progressStep = 10;
  size_t progressIncrement = (progressStep > 0) ? (fb->len / progressStep) : 0;
  if (progressIncrement == 0) {
    progressIncrement = fb->len;
  }
  size_t nextProgress = progressIncrement;

  while (sentBytes < fb->len) {
    bool indicationsEnabled = _dataDescriptor && _dataDescriptor->getIndications();
    bool notificationsEnabled = _dataDescriptor && _dataDescriptor->getNotifications();
    bool sendWithIndications = indicationsEnabled;

    if (!sendWithIndications && _connectionId != kInvalidConnId) {
      uint16_t waitIterations = 0;
      while (esp_ble_get_cur_sendable_packets_num(_connectionId) == 0) {
        delay(2);
        waitIterations++;
        if (waitIterations > 100) {
          notifyStatus("error:notify_buffer_full");
          aborted = true;
          break;
        }
      }
      if (aborted) {
        break;
      }
    }

    if (!_clientConnected || !_dataDescriptor ||
        (!notificationsEnabled && !indicationsEnabled)) {
      aborted = true;
      break;
    }

    size_t remaining = fb->len - sentBytes;
    size_t chunk = remaining < maxPayload ? remaining : maxPayload;

    _dataCharacteristic->setValue(fb->buf + sentBytes, chunk);
    if (sendWithIndications) {
      _dataCharacteristic->indicate();
    } else {
      _dataCharacteristic->notify();
    }
    sentBytes += chunk;

    if (sentBytes >= nextProgress && sentBytes < fb->len) {
      String progressPayload = "progress:";
      progressPayload += sentBytes;
      progressPayload += ":";
      progressPayload += fb->len;
      notifyStatus(progressPayload);
      nextProgress += progressIncrement;
    }

    delay(1);  // yield to BLE stack
  }

  releaseFrameBuffer(fb);

  unsigned long transferDuration = millis() - transferStart;

  if (aborted) {
    notifyStatus(String("error:transfer_aborted:") + sentBytes);
  } else {
    String donePayload = "done:";
    donePayload += photoIndex;
    donePayload += ":";
    donePayload += sentBytes;
    donePayload += ":";
    donePayload += transferDuration;
    notifyStatus(donePayload);
    _lastTransferMs = millis();
  }

  _transferInProgress = false;
}

void BLEImageService::notifyStatus(const String& payload) {
  if (_controlCharacteristic) {
    bool indicationsEnabled = _controlDescriptor && _controlDescriptor->getIndications();
    bool notificationsEnabled = _controlDescriptor && _controlDescriptor->getNotifications();

    if (!indicationsEnabled && !notificationsEnabled) {
      return;
    }

    if (!indicationsEnabled && _connectionId != kInvalidConnId) {
      uint16_t waitCount = 0;
      while (esp_ble_get_cur_sendable_packets_num(_connectionId) == 0) {
        delay(2);
        if (++waitCount > 100) {
          Serial.println("[" + getTimestamp() + "] BLE IMG: warn control buffer full");
          break;
        }
      }
    }

    _controlCharacteristic->setValue(payload.c_str());
    if (_clientConnected) {
      if (indicationsEnabled) {
        _controlCharacteristic->indicate();
      } else if (notificationsEnabled) {
        _controlCharacteristic->notify();
      }
    }
  }
  Serial.println("[" + getTimestamp() + "] BLE IMG: " + payload);
}

void BLEImageService::onClientConnected(uint16_t connId) {
  _clientConnected = true;
  if (connId != kInvalidConnId) {
    _connectionId = connId;
  }
  notifyStatus("client:connected");
}

void BLEImageService::onClientDisconnected() {
  _clientConnected = false;
  _captureRequested = false;
  _transferInProgress = false;
  _connectionId = kInvalidConnId;
  notifyStatus("client:disconnected");
}
