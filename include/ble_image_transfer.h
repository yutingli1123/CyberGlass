#ifndef BLE_IMAGE_TRANSFER_H
#define BLE_IMAGE_TRANSFER_H

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// BLE Characteristics for Image Transfer
#define BLE_CHAR_IMAGE_REQUEST_UUID                                                                                    \
  "e3e6c310-b762-11f0-a4f8-d323d6ee8628" // WRITE - Request image capture (params: resolution_index, quality)
#define BLE_CHAR_IMAGE_INFO_UUID                                                                                       \
  "f182b9d4-b762-11f0-8cab-7b33d60d040f" // READ/NOTIFY - Image metadata (size, chunks, status)
#define BLE_CHAR_IMAGE_DATA_UUID "f5009d24-b762-11f0-9826-2f5155dc5a7b" // READ/NOTIFY - Image data chunks
#define BLE_CHAR_IMAGE_CONTROL_UUID                                                                                    \
  "f79a5a02-b762-11f0-9a55-0fae30ddfe0c" // WRITE - Control transfer (request chunk, cancel)

// Image Transfer Configuration
#define BLE_IMAGE_CHUNK_SIZE 480 // Bytes per chunk (fits within BLE MTU)
#define BLE_IMAGE_MAX_SIZE 65535 // Maximum image size (64KB)

// BLE Image Transfer Class
class BLEImageTransfer {
public:
  BLEImageTransfer();

  // Initialize BLE image transfer characteristics (call after BLE server is created)
  bool initCharacteristics(BLEServer *pServer, BLEService *pService);

  // Image Transfer Functions
  bool captureAndPrepareImage(uint8_t resolutionIndex, uint8_t quality);
  bool sendImageChunk(uint16_t chunkIndex);
  void cancelImageTransfer();
  bool isImageTransferActive() const;

  // Cleanup
  void cleanup();

private:
  // BLE Image Transfer Characteristics
  BLECharacteristic *pCharImageRequest;
  BLECharacteristic *pCharImageInfo;
  BLECharacteristic *pCharImageData;
  BLECharacteristic *pCharImageControl;

  // BLE Callbacks (stored to prevent memory leak)
  BLECharacteristicCallbacks *pImageRequestCallbacks;
  BLECharacteristicCallbacks *pImageControlCallbacks;

  // Image Transfer State
  uint8_t *imageBuffer;
  size_t imageSize;
  uint16_t totalChunks;
  uint16_t currentChunk;
  bool imageTransferActive;

  // Friend class for callbacks
  friend class ImageTransferCallbacks;
};

#endif // BLE_IMAGE_TRANSFER_H
