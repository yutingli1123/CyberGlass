#ifndef BLE_VIDEO_STREAM_H
#define BLE_VIDEO_STREAM_H

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// BLE Device Configuration
#define BLE_DEVICE_NAME "CyberGlass"

// BLE Services for Video Stream
#define BLE_VIDEO_SERVICE_UUID "503848c4-bce3-11f0-9ccd-bf30decea150" 
#define BLE_VIDEO_DATA_SERVICE_1_UUID "55a9c06c-bce3-11f0-a025-7fecb01921e9"
#define BLE_VIDEO_DATA_SERVICE_2_UUID "5a7c0b7c-bce3-11f0-b0e7-67cbb27841b4"

// BLE Characteristics for Video Stream
#define BLE_CHAR_VIDEO_INFO_UUID "62fccb60-bce3-11f0-9a02-c38e72d2d0c8"
#define BLE_CHAR_VIDEO_DATA_1_UUID "66f0e594-bce3-11f0-ac75-8b26179f0c8c"
#define BLE_CHAR_VIDEO_DATA_2_UUID "6accca16-bce3-11f0-aa05-17a54e5b82d7"
#define BLE_CHAR_VIDEO_DATA_3_UUID "6e12bdca-bce3-11f0-a24e-17df33e71289"
#define BLE_CHAR_VIDEO_DATA_4_UUID "716cb4e4-bce3-11f0-9bbc-838987d75d6a"
#define BLE_CHAR_VIDEO_DATA_5_UUID "74974f6c-bce3-11f0-8f1d-0f29ee6587b5"
#define BLE_CHAR_VIDEO_DATA_6_UUID "77c6710e-bce3-11f0-a955-638046cc804c"
#define BLE_CHAR_VIDEO_DATA_7_UUID "7b53517a-bce3-11f0-8118-7f24f2ff5f0f"
#define BLE_CHAR_VIDEO_DATA_8_UUID "7ee172c2-bce3-11f0-8828-574c4e3b235d"
#define BLE_CHAR_VIDEO_CONTROL_UUID "82832b8c-bce3-11f0-bb48-cf7a2d9f36a2"

// Number of parallel data channels
#define BLE_VIDEO_DATA_CHANNELS 8

// Video Transfer Configuration
#define BLE_VIDEO_CHUNK_SIZE 480
#define BLE_VIDEO_MAX_SIZE 65535

// BLE Video Stream Class
class BLEVideoStream {
public:
  BLEVideoStream();

  bool initBLE();
  bool startVideoStream(uint8_t resolutionIndex, uint8_t quality, uint8_t targetFps, uint8_t chunkDelayMs = 50);
  void stopVideoStream();
  bool isVideoStreamActive() const;
  void processVideoStream();
  String getDeviceName() const;
  void cleanup();

private:
  bool sendVideoChunk(uint16_t chunkIndex, int count = -1);
  void cancelVideoTransfer();
  bool isVideoTransferActive() const;

  BLEServer *pServer;
  BLEService *pVideoService;
  BLEService *pVideoDataService1;
  BLEService *pVideoDataService2;
  BLECharacteristic *pCharVideoInfo;
  BLECharacteristic *pCharVideoData[BLE_VIDEO_DATA_CHANNELS];
  BLECharacteristic *pCharVideoControl;
  BLEServerCallbacks *pServerCallbacks;
  BLECharacteristicCallbacks *pVideoControlCallbacks;

  uint8_t *videoBuffer;
  size_t videoSize;
  uint16_t totalChunks;
  uint16_t currentChunk;
  bool videoTransferActive;

  volatile bool hasPendingChunkRequests;
  uint16_t pendingChunkIndexes[256];
  volatile uint8_t pendingChunkCount;

  bool videoStreamActive;
  uint8_t videoResolutionIndex;
  uint8_t videoQuality;
  uint8_t videoTargetFps;
  uint8_t videoChunkDelayMs;
  uint32_t frameCount;
  unsigned long streamStartTime;
  unsigned long frameInterval;

  char deviceName[32];

  friend class VideoStreamCallbacks;
};

#endif // BLE_VIDEO_STREAM_H