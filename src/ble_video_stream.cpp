#include "ble_video_stream.h"
#include "camera_module.h"
#include "esp_camera.h"
#include "esp_system.h"

// BLE Server Callbacks for handling connections/disconnections
class CyberGlassBLEServerCallbacks final : public BLEServerCallbacks {
  BLEVideoStream *stream;

public:
  explicit CyberGlassBLEServerCallbacks(BLEVideoStream *s) : stream(s) {}

  void onConnect(BLEServer *pServer) override { Serial.println("BLE: Client connected"); }

  void onDisconnect(BLEServer *pServer) override {
    Serial.println("BLE: Client disconnected");

    // Stop video stream if active and reset state
    if (stream) {
      Serial.println("BLE: Requesting stream stop due to disconnect");
      stream->stopVideoStream();
    }

    // Restart advertising so other devices can connect
    BLEDevice::startAdvertising();
    Serial.println("BLE: Advertising restarted");
  }
};

// BLE Callback class for handling video stream requests
class VideoStreamCallbacks final : public BLECharacteristicCallbacks {
  BLEVideoStream *stream;

public:
  explicit VideoStreamCallbacks(BLEVideoStream *st) : stream(st) {}

  void onWrite(BLECharacteristic *pCharacteristic) override {
    const std::string uuid = pCharacteristic->getUUID().toString();
    const std::string value = pCharacteristic->getValue();

    Serial.printf("BLE: onWrite called - UUID: %s, Length: %d\n", uuid.c_str(), value.length());

    if (uuid == BLE_CHAR_VIDEO_CONTROL_UUID) {
      if (!value.empty()) {
        const uint8_t command = static_cast<uint8_t>(value[0]);
        if (command == 0) {
          // Cancel transfer
          Serial.println("BLE: Video transfer cancelled");
          stream->cancelVideoTransfer();
        } else if (command == 1 && value.length() >= 5) {
          // Minimum: [cmd, count_low, count_high, chunk1_low, chunk1_high]
          // Batch retransmit: [1, count_low, count_high, chunk1_low, chunk1_high, chunk2_low, chunk2_high, ...]
          const uint16_t count = static_cast<uint8_t>(value[1]) | (static_cast<uint8_t>(value[2]) << 8);

          Serial.printf("BLE: Retransmit request - %d chunks (data length: %d bytes)\n", count, value.length());

          // Debug: print raw bytes
          Serial.print("BLE: Raw data: ");
          for (int i = 0; i < value.length(); i++) {
            Serial.printf("0x%02X ", static_cast<uint8_t>(value[i]));
          }
          Serial.println();

          // Calculate how many chunk indexes we can parse from received data
          const int maxChunksFromData = (value.length() - 3) / 2; // Skip [cmd, count_low, count_high]
          const int chunksToProcess = min((int) count, min(maxChunksFromData, 8));

          Serial.printf("BLE: Can parse %d chunks from %d bytes, will process %d\n", maxChunksFromData, value.length(),
                        chunksToProcess);

          // Parse chunk indexes and store for later processing
          stream->pendingChunkCount = 0;
          for (int i = 0; i < chunksToProcess; i++) {
            const int dataIndex = 3 + i * 2; // Start from bytes[3], not bytes[2]
            if (dataIndex + 1 < value.length()) {
              const uint16_t chunkIndex =
                  static_cast<uint8_t>(value[dataIndex]) | (static_cast<uint8_t>(value[dataIndex + 1]) << 8);
              stream->pendingChunkIndexes[stream->pendingChunkCount++] = chunkIndex;
              Serial.printf("  - Chunk %d (bytes[%d,%d] = 0x%02X,0x%02X)\n", chunkIndex, dataIndex, dataIndex + 1,
                            static_cast<uint8_t>(value[dataIndex]), static_cast<uint8_t>(value[dataIndex + 1]));
            }
          }

          if (stream->pendingChunkCount > 0) {
            stream->hasPendingChunkRequests = true;
            Serial.printf("BLE: Queued %d chunks for retransmit\n", stream->pendingChunkCount);
          } else {
            Serial.println("BLE: WARNING - No chunks parsed from retransmit request!");
          }
        } else if (command == 2) {
          // Frame ACK: [2]
          // Client confirms full frame received
          Serial.println("BLE: Frame ACK received");
          stream->frameAckReceived = true;
        } else if (command == 3 && value.length() >= 3) {
          // Start video stream: [3, resolution_index, quality, chunk_delay_ms (optional)]
          const uint8_t resolutionIndex = static_cast<uint8_t>(value[1]);
          const uint8_t quality = static_cast<uint8_t>(value[2]);
          const uint8_t chunkDelayMs = (value.length() >= 4) ? static_cast<uint8_t>(value[3]) : 50; // Default 50ms
          Serial.printf("BLE: Start video stream - Resolution: %d, Quality: %d, ChunkDelay: %dms\n",
                        resolutionIndex, quality, chunkDelayMs);
          stream->startVideoStream(resolutionIndex, quality, chunkDelayMs);
        } else if (command == 4) {
          // Stop video stream: [4]
          Serial.println("BLE: Stop video stream");
          stream->stopVideoStream();
        }
      }
    }
  }
};

BLEVideoStream::BLEVideoStream() :
    pServer(nullptr), pVideoService(nullptr), pVideoDataService1(nullptr), pVideoDataService2(nullptr),
    pCharVideoInfo(nullptr), pCharVideoControl(nullptr), pServerCallbacks(nullptr), pVideoControlCallbacks(nullptr),
    videoBuffer(nullptr), videoSize(0), totalChunks(0), currentChunk(0), videoTransferActive(false),
    hasPendingChunkRequests(false), pendingChunkCount(0), videoStreamActive(false), videoResolutionIndex(2),
    videoQuality(50), frameCount(0), streamStartTime(0), startRequested(false),
    stopRequested(false), waitingForFrameAck(false), frameAckReceived(false) {
  for (int i = 0; i < BLE_VIDEO_DATA_CHANNELS; i++) {
    pCharVideoData[i] = nullptr;
  }
}

bool BLEVideoStream::initBLE() {
  Serial.println("=== Initializing BLE Video Stream ===");

  // Generate unique device name with MAC address suffix
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(deviceName, sizeof(deviceName), "%s-%02X%02X", BLE_DEVICE_NAME, mac[4], mac[5]);

  // Initialize BLE
  BLEDevice::init(deviceName);
  Serial.println("BLE device initialized: " + String(deviceName));

  // Create BLE Server
  pServer = BLEDevice::createServer();
  Serial.printf("BLE Server created: %p\n", pServer);

  // Set server callbacks for connection/disconnection events
  pServerCallbacks = new CyberGlassBLEServerCallbacks(this);
  pServer->setCallbacks(pServerCallbacks);

  // Create Video Control Service (Info and Control characteristics)
  Serial.println("Creating Video Control Service...");
  pVideoService = pServer->createService(BLE_VIDEO_SERVICE_UUID);
  Serial.printf("Video Control Service created: %p\n", pVideoService);

  // Create Video Info Characteristic (Read/Notify) - Video stream metadata
  Serial.println("Creating Video Info characteristic...");
  pCharVideoInfo = pVideoService->createCharacteristic(
      BLE_CHAR_VIDEO_INFO_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pCharVideoInfo->addDescriptor(new BLE2902());
  Serial.printf("Video Info created: %p\n", pCharVideoInfo);

  // Create Video Control Characteristic (Write) - Video stream control
  Serial.println("Creating Video Control characteristic...");
  pCharVideoControl = pVideoService->createCharacteristic(
      BLE_CHAR_VIDEO_CONTROL_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pVideoControlCallbacks = new VideoStreamCallbacks(this);
  pCharVideoControl->setCallbacks(pVideoControlCallbacks);
  Serial.printf("Video Control created: %p\n", pCharVideoControl);

  // Start the control service
  pVideoService->start();
  Serial.println("Video Control Service started (2 characteristics)!");

  // Create Data Service 1 for parallel video transfer (channels 1-4)
  Serial.println("Creating Video Data Service 1...");
  pVideoDataService1 = pServer->createService(BLE_VIDEO_DATA_SERVICE_1_UUID);
  Serial.printf("Video Data Service 1 created: %p\n", pVideoDataService1);

  // Create Video Data Characteristics for Service 1 (channels 1-4)
  const char *dataUUIDs1[4] = {BLE_CHAR_VIDEO_DATA_1_UUID, BLE_CHAR_VIDEO_DATA_2_UUID, BLE_CHAR_VIDEO_DATA_3_UUID,
                               BLE_CHAR_VIDEO_DATA_4_UUID};

  Serial.println("Creating Video Data characteristics 1-4...");
  for (int i = 0; i < 4; i++) {
    pCharVideoData[i] = pVideoDataService1->createCharacteristic(dataUUIDs1[i], BLECharacteristic::PROPERTY_READ |
                                                                                    BLECharacteristic::PROPERTY_NOTIFY);
    pCharVideoData[i]->addDescriptor(new BLE2902());
    Serial.printf("Video Data channel %d created: %p\n", i + 1, pCharVideoData[i]);
  }

  // Start the data service 1
  pVideoDataService1->start();
  Serial.println("Video Data Service 1 started (4 parallel channels)!");

  // Create Data Service 2 for parallel video transfer (channels 5-8)
  Serial.println("Creating Video Data Service 2...");
  pVideoDataService2 = pServer->createService(BLE_VIDEO_DATA_SERVICE_2_UUID);
  Serial.printf("Video Data Service 2 created: %p\n", pVideoDataService2);

  // Create Video Data Characteristics for Service 2 (channels 5-8)
  const char *dataUUIDs2[4] = {BLE_CHAR_VIDEO_DATA_5_UUID, BLE_CHAR_VIDEO_DATA_6_UUID, BLE_CHAR_VIDEO_DATA_7_UUID,
                               BLE_CHAR_VIDEO_DATA_8_UUID};

  Serial.println("Creating Video Data characteristics 5-8...");
  for (int i = 0; i < 4; i++) {
    pCharVideoData[i + 4] = pVideoDataService2->createCharacteristic(
        dataUUIDs2[i], BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pCharVideoData[i + 4]->addDescriptor(new BLE2902());
    Serial.printf("Video Data channel %d created: %p\n", i + 5, pCharVideoData[i + 4]);
  }

  // Start the data service 2
  pVideoDataService2->start();
  Serial.println("Video Data Service 2 started (4 parallel channels)!");

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_VIDEO_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising started");
  Serial.println("BLE Video Stream: 3 services started (Control + Data1 + Data2 = 8 channels)!");
  Serial.println("======================================");

  return true;
}

bool BLEVideoStream::sendVideoChunk(const uint16_t chunkIndex, const int count) {
  if (!videoBuffer) {
    Serial.println("No video buffer available");
    return false;
  }

  if (chunkIndex >= totalChunks) {
    Serial.printf("Invalid chunk index: %d (max: %d)\n", chunkIndex, totalChunks - 1);
    return false;
  }

  int chunksToSend;
  if (count == -1) {
    chunksToSend = min(BLE_VIDEO_DATA_CHANNELS, (int) (totalChunks - chunkIndex));
  } else {
    chunksToSend = min(count, (int) (totalChunks - chunkIndex));
  }

  for (int i = 0; i < chunksToSend; i++) {
    uint16_t currentChunkIndex = chunkIndex + i;

    const size_t chunkOffset = currentChunkIndex * BLE_VIDEO_CHUNK_SIZE;
    size_t chunkSize = BLE_VIDEO_CHUNK_SIZE;
    if (chunkOffset + chunkSize > videoSize) {
      chunkSize = videoSize - chunkOffset;
    }

    uint8_t chunkData[BLE_VIDEO_CHUNK_SIZE + 2];
    chunkData[0] = currentChunkIndex & 0xFF;
    chunkData[1] = (currentChunkIndex >> 8) & 0xFF;
    memcpy(chunkData + 2, videoBuffer + chunkOffset, chunkSize);

    pCharVideoData[i]->setValue(chunkData, chunkSize + 2);
    pCharVideoData[i]->notify();

    // Delay to prevent cellular BLE stack congestion
    delay(8);

    currentChunk = currentChunkIndex;
  }

  // Adaptive delay: wait longer if we are sending many chunks
  int baseDelay = videoStreamActive ? videoChunkDelayMs : 50;
  // Ensure we don't flood the stack even with the delay
  if (baseDelay < 10)
    baseDelay = 10;

  delay(baseDelay);

  Serial.printf("Sent chunks %d-%d/%d (batch of %d)\n", chunkIndex + 1, chunkIndex + chunksToSend, totalChunks,
                chunksToSend);

  return true;
}

void BLEVideoStream::cancelVideoTransfer() {
  if (videoBuffer) {
    free(videoBuffer);
    videoBuffer = nullptr;
    pendingChunkCount = 0;
    hasPendingChunkRequests = false;
  }
  videoSize = 0;
  totalChunks = 0;
  currentChunk = 0;
  videoTransferActive = false;
  waitingForFrameAck = false; // Reset ACK wait state

  if (pCharVideoInfo) {
    uint8_t videoInfo[7] = {0}; // Status: 0 = idle
    pCharVideoInfo->setValue(videoInfo, 7);
    pCharVideoInfo->notify();
  }

  Serial.println("Video transfer cancelled");
}

bool BLEVideoStream::isVideoTransferActive() const { return videoTransferActive; }

String BLEVideoStream::getDeviceName() const { return String(deviceName); }

void BLEVideoStream::cleanup() {
  stopVideoStream();
  cancelVideoTransfer();

  delete pServerCallbacks;
  delete pVideoControlCallbacks;

  pServerCallbacks = nullptr;
  pVideoControlCallbacks = nullptr;
  pCharVideoInfo = nullptr;
  for (int i = 0; i < BLE_VIDEO_DATA_CHANNELS; i++) {
    pCharVideoData[i] = nullptr;
  }
  pCharVideoControl = nullptr;

  if (pServer) {
    BLEDevice::deinit(true);
    pServer = nullptr;
    pVideoService = nullptr;
    pVideoDataService1 = nullptr;
    pVideoDataService2 = nullptr;
  }

  Serial.println("BLE Video Stream: Cleanup complete");
}

void BLEVideoStream::startVideoStream(const uint8_t resolutionIndex, const uint8_t quality,
                                      const uint8_t chunkDelayMs) {
  Serial.println("=== Starting Video Stream (Requested) ===");

  // Store params and set flag for processing in main loop
  pendingParams.resolutionIndex = resolutionIndex;
  pendingParams.quality = quality;
  pendingParams.chunkDelayMs = chunkDelayMs;

  startRequested = true;
}

void BLEVideoStream::performStartVideoStream() {
  uint8_t resolutionIndex = pendingParams.resolutionIndex;
  uint8_t quality = pendingParams.quality;
  uint8_t chunkDelayMs = pendingParams.chunkDelayMs;

  Serial.println("=== Performing Start Video Stream ===");

  cancelVideoTransfer();

  if (videoStreamActive) {
    performStopVideoStream(); // Ensure we are stopped
  }

  // Wake up camera
  if (!sleepCamera(false)) {
    Serial.println("Failed to wake camera!");
    return;
  }

  // Boost CPU frequency for performance
  setCpuFrequencyMhz(240);
  Serial.printf("CPU Frequency set to %d MHz\n", getCpuFrequencyMhz());

  videoResolutionIndex = resolutionIndex;
  videoQuality = quality;
  videoChunkDelayMs = chunkDelayMs;
  frameCount = 0;
  streamStartTime = millis();

  // Reset Flow Control State
  waitingForFrameAck = false;
  frameAckReceived = false;

  const framesize_t resolutions[] = {FRAMESIZE_QQVGA, FRAMESIZE_QVGA, FRAMESIZE_VGA,  FRAMESIZE_SVGA,
                                     FRAMESIZE_XGA,   FRAMESIZE_HD,   FRAMESIZE_SXGA, FRAMESIZE_UXGA};

  framesize_t targetResolution = FRAMESIZE_VGA;
  if (resolutionIndex < 8) {
    targetResolution = resolutions[resolutionIndex];
  }

  if (!changeResolution(targetResolution)) {
    Serial.println("Failed to change resolution");
    return;
  }

  if (!changeQuality(quality)) {
    Serial.println("Failed to change quality");
  }

  videoStreamActive = true;

  Serial.printf("Video stream started: Resolution=%d, Quality=%d, ChunkDelay=%d ms\n",
                resolutionIndex, quality, chunkDelayMs);

  if (pCharVideoInfo) {
    uint8_t streamInfo[7];
    streamInfo[0] = 5;
    streamInfo[1] = resolutionIndex;
    streamInfo[2] = quality;
    streamInfo[3] = 0; // Removed FPS
    streamInfo[4] = 0;
    streamInfo[5] = 0;
    streamInfo[6] = 0;
    pCharVideoInfo->setValue(streamInfo, 7);
    pCharVideoInfo->notify();
    Serial.println("Video stream status sent");
  }

  Serial.println("======================================");
}

void BLEVideoStream::stopVideoStream() {
  Serial.println("=== Stopping Video Stream (Requested) ===");
  stopRequested = true;
  startRequested = false; // Cancel any pending start request
}

void BLEVideoStream::performStopVideoStream() {
  if (!videoStreamActive) {
    return;
  }

  Serial.println("=== Performing Stop Video Stream ===");
  videoStreamActive = false;

  // Reduce CPU frequency to save power
  setCpuFrequencyMhz(80);
  Serial.printf("CPU Frequency set to %d MHz\n", getCpuFrequencyMhz());

  // Put camera to sleep
  sleepCamera(true);

  if (videoBuffer) {
    free(videoBuffer);
    videoBuffer = nullptr;
  }
  videoSize = 0;
  totalChunks = 0;
  currentChunk = 0;

  if (pCharVideoInfo) {
    uint8_t streamInfo[7] = {0};
    pCharVideoInfo->setValue(streamInfo, 7);
    pCharVideoInfo->notify();
    Serial.println("Video stream stopped notification sent");
  }

  Serial.printf("Video stream stopped after %lu frames\n", frameCount);
  Serial.println("======================================");
}

bool BLEVideoStream::isVideoStreamActive() const { return videoStreamActive; }

void BLEVideoStream::processVideoStream() {
  // Handle start/stop requests from BLE callback
  if (stopRequested) {
    stopRequested = false;
    performStopVideoStream();
  }

  if (startRequested) {
    startRequested = false;
    performStartVideoStream();
  }

  if (!videoStreamActive) {
    return;
  }

  // Handle pending chunk retransmissions (Priority over new frames)
  if (hasPendingChunkRequests) {
    hasPendingChunkRequests = false; // Reset flag

    if (videoBuffer && pendingChunkCount > 0) {
      Serial.printf("BLE: Retransmitting %d chunks...\n", pendingChunkCount);
      for (int i = 0; i < pendingChunkCount; i++) {
        // Send single chunk
        sendVideoChunk(pendingChunkIndexes[i], 1);
      }
    }
    pendingChunkCount = 0;

    // If we were waiting for ACK, receiving a NACK means we stay in waiting state
    if (waitingForFrameAck) {
      // logic to stay waiting is implicit
    }
  }

  if (videoTransferActive) {
    return;
  }

  // --- FRAME-LEVEL FLOW CONTROL ---
  if (waitingForFrameAck) {
    // Check for ACK
    if (frameAckReceived) {
      Serial.println("BLE: Frame confirmed (ACK), proceeding to next frame");
      waitingForFrameAck = false;
      frameAckReceived = false;
      // Proceed effectively immediately to capture next frame
    }
    // Still waiting...
    else {
      return; // BLOCK new frame capture indefinitely until ACK or Stop
    }
  }



  if (videoBuffer) {
    free(videoBuffer);
    videoBuffer = nullptr;
    // CRITICAL: Clear any pending retransmits for the old frame
    // otherwise we might send chunks of the NEW frame to satisfy a request for the OLD frame
    pendingChunkCount = 0;
    hasPendingChunkRequests = false;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Video frame capture failed");
    return;
  }

  if (fb->len > BLE_VIDEO_MAX_SIZE) {
    Serial.printf("Video frame too large: %d bytes (max: %d)\n", fb->len, BLE_VIDEO_MAX_SIZE);
    esp_camera_fb_return(fb);
    return;
  }

  videoBuffer = static_cast<uint8_t *>(malloc(fb->len));
  if (!videoBuffer) {
    Serial.println("Failed to allocate frame buffer");
    esp_camera_fb_return(fb);
    return;
  }

  memcpy(videoBuffer, fb->buf, fb->len);
  videoSize = fb->len;
  esp_camera_fb_return(fb);

  totalChunks = (videoSize + BLE_VIDEO_CHUNK_SIZE - 1) / BLE_VIDEO_CHUNK_SIZE;
  currentChunk = 0;
  frameCount++;
  videoTransferActive = true;

  // Enter Waiting State immediately for the new frame
  // Note: We only effectively wait AFTER sending is done, but we init the state here
  waitingForFrameAck = false;
  frameAckReceived = false;

  if (pCharVideoInfo) {
    // ... send info ...
    uint8_t frameInfo[7];
    frameInfo[0] = 6;
    frameInfo[1] = (frameCount & 0xFF);
    frameInfo[2] = ((frameCount >> 8) & 0xFF);
    frameInfo[3] = ((frameCount >> 16) & 0xFF);
    frameInfo[4] = ((frameCount >> 24) & 0xFF);
    frameInfo[5] = totalChunks & 0xFF;
    frameInfo[6] = (totalChunks >> 8) & 0xFF;
    pCharVideoInfo->setValue(frameInfo, 7);
    pCharVideoInfo->notify();
  }

  for (uint16_t i = 0; i < totalChunks; i += BLE_VIDEO_DATA_CHANNELS) {
    sendVideoChunk(i);
  }

  videoTransferActive = false;

  // Transfer complete, now start waiting for ACK
  waitingForFrameAck = true;


  if (frameCount % 10 == 0) {
    unsigned long elapsed = millis() - streamStartTime;
    float fps = (frameCount * 1000.0) / elapsed;
    Serial.printf("Video: Frame %lu sent (%u bytes) - Avg FPS: %.2f\n", frameCount, videoSize, fps);
  }
}
