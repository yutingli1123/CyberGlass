/**
 * @file ble_image_service.h
 * @brief BLE image transfer service for CyberGlass
 */

#ifndef BLE_IMAGE_SERVICE_H
#define BLE_IMAGE_SERVICE_H

#include <Arduino.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "camera_module.h"

// BLE UUIDs for image transfer service
#define BLE_IMAGE_SERVICE_UUID        "b0809f0c-691c-4ffa-a5a3-651350a1479a"
#define BLE_IMAGE_CONTROL_CHAR_UUID   "b0809f0d-691c-4ffa-a5a3-651350a1479a"
#define BLE_IMAGE_DATA_CHAR_UUID      "b0809f0e-691c-4ffa-a5a3-651350a1479a"

class BLEImageService {
public:
  BLEImageService();

  /**
   * @brief Initialize BLE image transfer service using existing BLE server
   * @param server Pointer to shared BLE server (created by provisioning module)
   * @param deviceLabel Friendly label for status messages (e.g., device ID)
   * @return true if service initialized successfully
   */
  bool begin(BLEServer* server, const String& deviceLabel);

  /**
   * @brief Process pending BLE transfer events
   * Should be called from main loop regularly.
   */
  void loop();

  /**
   * @brief Stop ongoing transfer and reset service state
   */
  void stop();

  /**
   * @brief Check if a BLE client is connected and subscribed
   */
  bool readyForTransfer();

private:
  class ControlCallbacks : public BLECharacteristicCallbacks {
  public:
    explicit ControlCallbacks(BLEImageService* parent) : _parent(parent) {}
    void onWrite(BLECharacteristic* characteristic) override;

  private:
    BLEImageService* _parent;
  };

  class ServerCallbacks : public BLEServerCallbacks {
  public:
    explicit ServerCallbacks(BLEImageService* parent) : _parent(parent) {}
    void onConnect(BLEServer* pServer) override;
    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) override;
    void onDisconnect(BLEServer* pServer) override;
    void onDisconnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) override;

  private:
    BLEImageService* _parent;
  };

  void handleCommand(const std::string& command);
  void processCaptureRequest();
  void notifyStatus(const String& payload);
  void onClientConnected(uint16_t connId = 0xFFFF);
  void onClientDisconnected();

  BLEServer* _server;
  BLEService* _service;
  BLECharacteristic* _controlCharacteristic;
  BLECharacteristic* _dataCharacteristic;
  BLE2902* _controlDescriptor;
  BLE2902* _dataDescriptor;

  String _deviceLabel;
  bool _captureRequested;
  bool _transferInProgress;
  bool _clientConnected;

  unsigned long _lastTransferMs;
  uint16_t _connectionId;
};

#endif // BLE_IMAGE_SERVICE_H
