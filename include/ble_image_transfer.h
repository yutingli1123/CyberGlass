#ifndef BLE_IMAGE_TRANSFER_H
#define BLE_IMAGE_TRANSFER_H

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// BLE Device Configuration
#define BLE_DEVICE_NAME "CyberGlass"

// BLE Services for Image Transfer
#define BLE_IMAGE_SERVICE_UUID "503848c4-bce3-11f0-9ccd-bf30decea150" // Control service (Request, Info, Control)
#define BLE_IMAGE_DATA_SERVICE_1_UUID "55a9c06c-bce3-11f0-a025-7fecb01921e9" // Data service 1 (4 parallel channels)
#define BLE_IMAGE_DATA_SERVICE_2_UUID "5a7c0b7c-bce3-11f0-b0e7-67cbb27841b4" // Data service 2 (4 parallel channels)

// BLE Characteristics for Video Stream
#define BLE_CHAR_IMAGE_INFO_UUID                                                                                       \
  "62fccb60-bce3-11f0-9a02-c38e72d2d0c8" // READ/NOTIFY - Video stream metadata (status, frame info)
#define BLE_CHAR_IMAGE_DATA_1_UUID "66f0e594-bce3-11f0-ac75-8b26179f0c8c" // READ/NOTIFY - Image data chunks (channel 1)
#define BLE_CHAR_IMAGE_DATA_2_UUID "6accca16-bce3-11f0-aa05-17a54e5b82d7" // READ/NOTIFY - Image data chunks (channel 2)
#define BLE_CHAR_IMAGE_DATA_3_UUID "6e12bdca-bce3-11f0-a24e-17df33e71289" // READ/NOTIFY - Image data chunks (channel 3)
#define BLE_CHAR_IMAGE_DATA_4_UUID "716cb4e4-bce3-11f0-9bbc-838987d75d6a" // READ/NOTIFY - Image data chunks (channel 4)
#define BLE_CHAR_IMAGE_DATA_5_UUID "74974f6c-bce3-11f0-8f1d-0f29ee6587b5" // READ/NOTIFY - Image data chunks (channel 5)
#define BLE_CHAR_IMAGE_DATA_6_UUID "77c6710e-bce3-11f0-a955-638046cc804c" // READ/NOTIFY - Image data chunks (channel 6)
#define BLE_CHAR_IMAGE_DATA_7_UUID "7b53517a-bce3-11f0-8118-7f24f2ff5f0f" // READ/NOTIFY - Image data chunks (channel 7)
#define BLE_CHAR_IMAGE_DATA_8_UUID "7ee172c2-bce3-11f0-8828-574c4e3b235d" // READ/NOTIFY - Image data chunks (channel 8)
#define BLE_CHAR_IMAGE_CONTROL_UUID "82832b8c-bce3-11f0-bb48-cf7a2d9f36a2" // WRITE - Control transfer (request chunk, cancel)


// Number of parallel data channels
#define BLE_IMAGE_DATA_CHANNELS 8

// Image Transfer Configuration
#define BLE_IMAGE_CHUNK_SIZE 480 // Bytes per chunk (fits within BLE MTU)
#define BLE_IMAGE_MAX_SIZE 65535 // Maximum image size (64KB)

// BLE Image Transfer Class
class BLEImageTransfer {
public:
  BLEImageTransfer();

  // Initialize BLE device and services
  bool initBLE();

  // Video Stream Functions
  bool startVideoStream(uint8_t resolutionIndex, uint8_t quality, uint8_t targetFps, uint8_t chunkDelayMs = 50);

  void stopVideoStream();

  bool isVideoStreamActive() const;

  void processVideoStream(); // Call from main loop

  // Get device name
  String getDeviceName() const;

  // Process pending requests (call from main loop)
  void processPendingRequests();

  // Cleanup
  void cleanup();

private:
  // Internal helper functions (used by video streaming)
  bool sendImageChunk(uint16_t chunkIndex, int count = -1); // count=-1: send batch, count=1: send single
  void cancelImageTransfer();
  bool isImageTransferActive() const;

  // BLE Server
  BLEServer *pServer;

  // BLE Image Transfer Services
  BLEService *pImageService; // Control service (Request, Info, Control)
  BLEService *pImageDataService1; // Data service 1 (channels 1-4)
  BLEService *pImageDataService2; // Data service 2 (channels 5-8)

  // BLE Video Transfer Characteristics
  BLECharacteristic *pCharImageInfo;
  BLECharacteristic *pCharImageData[BLE_IMAGE_DATA_CHANNELS]; // Multiple data channels for parallel transfer
  BLECharacteristic *pCharImageControl;

  // BLE Callbacks (stored to prevent memory leak)
  BLEServerCallbacks *pServerCallbacks;
  BLECharacteristicCallbacks *pImageControlCallbacks;

  // Video Frame Transfer State
  uint8_t *imageBuffer;
  size_t imageSize;
  uint16_t totalChunks;
  uint16_t currentChunk;
  bool imageTransferActive;

  // Pending chunk requests (batch retransmit support for video frames)
  volatile bool hasPendingChunkRequests;
  uint16_t pendingChunkIndexes[256]; // Fixed size array (max 256 chunks to retransmit at once)
  volatile uint8_t pendingChunkCount;

  // Video Stream State
  bool videoStreamActive;
  uint8_t videoResolutionIndex;
  uint8_t videoQuality;
  uint8_t videoTargetFps;
  uint8_t videoChunkDelayMs; // Delay between chunk batches (ms)
  uint32_t frameCount;
  unsigned long streamStartTime;
  unsigned long frameInterval; // milliseconds between frames (1000/fps)

  // Device name storage
  char deviceName[32];

  // Friend class for callbacks
  friend class ImageTransferCallbacks;
};

#endif // BLE_IMAGE_TRANSFER_H