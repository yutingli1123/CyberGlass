#ifndef BLE_IMAGE_TRANSFER_H
#define BLE_IMAGE_TRANSFER_H

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// BLE Services for Image Transfer (separate from WiFi provisioning service)
#define BLE_IMAGE_SERVICE_UUID "c6116a0a-b7a0-11f0-880d-6baf85e562fd"  // Control service (Request, Info, Control)
#define BLE_IMAGE_DATA_SERVICE_UUID "d8227b1c-c1d5-11f0-9f3e-4c6a95f7e8d1"  // Data service (4 parallel channels)

// BLE Characteristics for Image Transfer
#define BLE_CHAR_IMAGE_REQUEST_UUID                                                                                    \
  "e3e6c310-b762-11f0-a4f8-d323d6ee8628" // WRITE - Request image capture (params: resolution_index, quality)
#define BLE_CHAR_IMAGE_INFO_UUID                                                                                       \
  "f182b9d4-b762-11f0-8cab-7b33d60d040f" // READ/NOTIFY - Image metadata (size, chunks, status)
#define BLE_CHAR_IMAGE_DATA_1_UUID "f5009d24-b762-11f0-9826-2f5155dc5a7b" // READ/NOTIFY - Image data chunks (channel 1)
#define BLE_CHAR_IMAGE_DATA_2_UUID "a8c72f3e-c1d4-11f0-b2a5-9f4e61bc8d2a" // READ/NOTIFY - Image data chunks (channel 2)
#define BLE_CHAR_IMAGE_DATA_3_UUID "b3d84a52-c1d4-11f0-8f7c-1a5d92e3c4b6" // READ/NOTIFY - Image data chunks (channel 3)
#define BLE_CHAR_IMAGE_DATA_4_UUID "bd9e5c68-c1d4-11f0-9e4d-3b7a84f5d2c9" // READ/NOTIFY - Image data chunks (channel 4)
#define BLE_CHAR_IMAGE_CONTROL_UUID                                                                                    \
  "f79a5a02-b762-11f0-9a55-0fae30ddfe0c" // WRITE - Control transfer (request chunk, cancel)

// Number of parallel data channels
#define BLE_IMAGE_DATA_CHANNELS 4

// Image Transfer Configuration
#define BLE_IMAGE_CHUNK_SIZE 480 // Bytes per chunk (fits within BLE MTU)
#define BLE_IMAGE_MAX_SIZE 65535 // Maximum image size (64KB)

// BLE Image Transfer Class
class BLEImageTransfer {
public:
  BLEImageTransfer();

  // Initialize BLE image transfer with its own service
  bool initService(BLEServer *pServer);

  // Image Transfer Functions
  bool captureAndPrepareImage(uint8_t resolutionIndex, uint8_t quality);
  bool sendImageChunk(uint16_t chunkIndex);
  void cancelImageTransfer();
  bool isImageTransferActive() const;

  // Process pending requests (call from main loop)
  void processPendingRequests();

  // Cleanup
  void cleanup();

private:
  // BLE Image Transfer Services (separate from WiFi service)
  BLEService *pImageService;      // Control service (Request, Info, Control)
  BLEService *pImageDataService;  // Data service (4 parallel channels)

  // BLE Image Transfer Characteristics
  BLECharacteristic *pCharImageRequest;
  BLECharacteristic *pCharImageInfo;
  BLECharacteristic *pCharImageData[BLE_IMAGE_DATA_CHANNELS]; // Multiple data channels for parallel transfer
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

  // Pending request
  volatile bool hasPendingRequest;
  uint8_t pendingResolutionIndex;
  uint8_t pendingQuality;

  // Pending chunk requests
  volatile bool hasPendingChunkRequest;
  uint16_t pendingChunkIndex;

  // Friend class for callbacks
  friend class ImageTransferCallbacks;
};

#endif // BLE_IMAGE_TRANSFER_H
