#include "ble_image_transfer.h"
#include "camera_module.h"
#include "esp_camera.h"
#include "esp_system.h"

// BLE Server Callbacks for handling connections/disconnections
class CyberGlassBLEServerCallbacks final : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override { 
    Serial.println("BLE: Client connected"); 
  }

  void onDisconnect(BLEServer *pServer) override {
    Serial.println("BLE: Client disconnected");
    // Restart advertising so other devices can connect
    BLEDevice::startAdvertising();
    Serial.println("BLE: Advertising restarted");
  }
};

// BLE Callback class for handling image transfer requests
class ImageTransferCallbacks final : public BLECharacteristicCallbacks {
  BLEImageTransfer *transfer;

public:
  explicit ImageTransferCallbacks(BLEImageTransfer *xfer) : transfer(xfer) {}

  void onWrite(BLECharacteristic *pCharacteristic) override {
    const std::string uuid = pCharacteristic->getUUID().toString();
    const std::string value = pCharacteristic->getValue();

    Serial.printf("BLE: onWrite called - UUID: %s, Length: %d\n", uuid.c_str(), value.length());

    if (uuid == BLE_CHAR_IMAGE_REQUEST_UUID) {
      // Format: [resolution_index, quality]
      if (value.length() >= 2) {
        const uint8_t resolutionIndex = static_cast<uint8_t>(value[0]);
        const uint8_t quality = static_cast<uint8_t>(value[1]);
        Serial.printf("BLE: Image request queued - Resolution: %d, Quality: %d\n", resolutionIndex, quality);

        // New request received - release previous image buffer if exists
        if (transfer->imageBuffer) {
          Serial.println("BLE: New request received, releasing previous image buffer");
          free(transfer->imageBuffer);
          transfer->imageBuffer = nullptr;
          transfer->imageTransferActive = false;
        }

        // Queue the request instead of processing immediately
        transfer->pendingResolutionIndex = resolutionIndex;
        transfer->pendingQuality = quality;
        transfer->hasPendingRequest = true;
      }
    } else if (uuid == BLE_CHAR_IMAGE_CONTROL_UUID) {
      if (!value.empty()) {
        const uint8_t command = static_cast<uint8_t>(value[0]);
        if (command == 0) {
          // Cancel transfer
          Serial.println("BLE: Image transfer cancelled");
          transfer->cancelImageTransfer();
        } else if (command == 1 && value.length() >= 5) {  // Minimum: [cmd, count_low, count_high, chunk1_low, chunk1_high]
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
          const int maxChunksFromData = (value.length() - 3) / 2;  // Skip [cmd, count_low, count_high]
          const int chunksToProcess = min((int)count, min(maxChunksFromData, 8));
          
          Serial.printf("BLE: Can parse %d chunks from %d bytes, will process %d\n", 
                       maxChunksFromData, value.length(), chunksToProcess);
          
          // Parse chunk indexes and store for later processing
          transfer->pendingChunkCount = 0;
          for (int i = 0; i < chunksToProcess; i++) {
            const int dataIndex = 3 + i*2;  // Start from bytes[3], not bytes[2]
            if (dataIndex + 1 < value.length()) {
              const uint16_t chunkIndex = static_cast<uint8_t>(value[dataIndex]) | 
                                         (static_cast<uint8_t>(value[dataIndex + 1]) << 8);
              transfer->pendingChunkIndexes[transfer->pendingChunkCount++] = chunkIndex;
              Serial.printf("  - Chunk %d (bytes[%d,%d] = 0x%02X,0x%02X)\n", 
                           chunkIndex, dataIndex, dataIndex+1, 
                           static_cast<uint8_t>(value[dataIndex]), static_cast<uint8_t>(value[dataIndex+1]));
            }
          }
          
          if (transfer->pendingChunkCount > 0) {
            transfer->hasPendingChunkRequests = true;
            Serial.printf("BLE: Queued %d chunks for retransmit\n", transfer->pendingChunkCount);
          } else {
            Serial.println("BLE: WARNING - No chunks parsed from retransmit request!");
          }
        }
        // Removed command == 2 confirmation mechanism
        // Image buffer is now released when new request arrives
      }
    }
  }
};

BLEImageTransfer::BLEImageTransfer() :
    pServer(nullptr), pImageService(nullptr), pImageDataService1(nullptr), pImageDataService2(nullptr), 
    pCharImageRequest(nullptr), pCharImageInfo(nullptr), pCharImageControl(nullptr), 
    pServerCallbacks(nullptr), pImageRequestCallbacks(nullptr), pImageControlCallbacks(nullptr), 
    imageBuffer(nullptr), imageSize(0), totalChunks(0), currentChunk(0),
    imageTransferActive(false), hasPendingRequest(false), pendingResolutionIndex(0), pendingQuality(0),
    hasPendingChunkRequests(false), pendingChunkCount(0), queuedNotifyCount(0) {
  for (int i = 0; i < BLE_IMAGE_DATA_CHANNELS; i++) {
    pCharImageData[i] = nullptr;
  }
}

bool BLEImageTransfer::initBLE() {
  Serial.println("=== Initializing BLE Image Transfer ===");

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
  pServerCallbacks = new CyberGlassBLEServerCallbacks();
  pServer->setCallbacks(pServerCallbacks);

  // Create Control Service for Image Transfer (Request, Info, Control)
  Serial.println("Creating Image Control Service...");
  pImageService = pServer->createService(BLE_IMAGE_SERVICE_UUID);
  Serial.printf("Image Control Service created: %p\n", pImageService);

  // Create Image Request Characteristic (Write) - Request image capture
  Serial.println("Creating Image Request characteristic...");
  pCharImageRequest = pImageService->createCharacteristic(
      BLE_CHAR_IMAGE_REQUEST_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  Serial.printf("Image Request created: %p\n", pCharImageRequest);
  pImageRequestCallbacks = new ImageTransferCallbacks(this);
  pCharImageRequest->setCallbacks(pImageRequestCallbacks);

  // Create Image Info Characteristic (Read/Notify) - Image metadata
  Serial.println("Creating Image Info characteristic...");
  pCharImageInfo = pImageService->createCharacteristic(
      BLE_CHAR_IMAGE_INFO_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pCharImageInfo->addDescriptor(new BLE2902());
  Serial.printf("Image Info created: %p\n", pCharImageInfo);

  // Create Image Control Characteristic (Write) - Control transfer
  Serial.println("Creating Image Control characteristic...");
  pCharImageControl = pImageService->createCharacteristic(
      BLE_CHAR_IMAGE_CONTROL_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pImageControlCallbacks = new ImageTransferCallbacks(this);
  pCharImageControl->setCallbacks(pImageControlCallbacks);
  Serial.printf("Image Control created: %p\n", pCharImageControl);

  // Start the control service
  pImageService->start();
  Serial.println("Image Control Service started (3 characteristics)!");

  // Create Data Service 1 for parallel image transfer (channels 1-4)
  Serial.println("Creating Image Data Service 1...");
  pImageDataService1 = pServer->createService(BLE_IMAGE_DATA_SERVICE_1_UUID);
  Serial.printf("Image Data Service 1 created: %p\n", pImageDataService1);

  // Create Image Data Characteristics for Service 1 (channels 1-4)
  const char *dataUUIDs1[4] = {BLE_CHAR_IMAGE_DATA_1_UUID, BLE_CHAR_IMAGE_DATA_2_UUID, BLE_CHAR_IMAGE_DATA_3_UUID,
                               BLE_CHAR_IMAGE_DATA_4_UUID};

  Serial.println("Creating Image Data characteristics 1-4...");
  for (int i = 0; i < 4; i++) {
    pCharImageData[i] = pImageDataService1->createCharacteristic(dataUUIDs1[i], BLECharacteristic::PROPERTY_READ |
                                                                                    BLECharacteristic::PROPERTY_NOTIFY);
    pCharImageData[i]->addDescriptor(new BLE2902());
    Serial.printf("Image Data channel %d created: %p\n", i + 1, pCharImageData[i]);
  }

  // Start the data service 1
  pImageDataService1->start();
  Serial.println("Image Data Service 1 started (4 parallel channels)!");

  // Create Data Service 2 for parallel image transfer (channels 5-8)
  Serial.println("Creating Image Data Service 2...");
  pImageDataService2 = pServer->createService(BLE_IMAGE_DATA_SERVICE_2_UUID);
  Serial.printf("Image Data Service 2 created: %p\n", pImageDataService2);

  // Create Image Data Characteristics for Service 2 (channels 5-8)
  const char *dataUUIDs2[4] = {BLE_CHAR_IMAGE_DATA_5_UUID, BLE_CHAR_IMAGE_DATA_6_UUID, BLE_CHAR_IMAGE_DATA_7_UUID,
                               BLE_CHAR_IMAGE_DATA_8_UUID};

  Serial.println("Creating Image Data characteristics 5-8...");
  for (int i = 0; i < 4; i++) {
    pCharImageData[i + 4] = pImageDataService2->createCharacteristic(
        dataUUIDs2[i], BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pCharImageData[i + 4]->addDescriptor(new BLE2902());
    Serial.printf("Image Data channel %d created: %p\n", i + 5, pCharImageData[i + 4]);
  }

  // Start the data service 2
  pImageDataService2->start();
  Serial.println("Image Data Service 2 started (4 parallel channels)!");

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_IMAGE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising started");
  Serial.println("BLE Image Transfer: 3 services started (Control + Data1 + Data2 = 8 channels)!");
  Serial.println("======================================");

  return true;
}

bool BLEImageTransfer::captureAndPrepareImage(const uint8_t resolutionIndex, const uint8_t quality) {
  Serial.println("=== Capturing Image for BLE Transfer ===");

  // Cancel any existing transfer
  cancelImageTransfer();

  // Map resolution index to framesize_t
  const framesize_t resolutions[] = {FRAMESIZE_QQVGA, // 0: 160x120
                                     FRAMESIZE_QVGA, // 1: 320x240
                                     FRAMESIZE_VGA, // 2: 640x480
                                     FRAMESIZE_SVGA, // 3: 800x600
                                     FRAMESIZE_XGA, // 4: 1024x768
                                     FRAMESIZE_HD, // 5: 1280x720
                                     FRAMESIZE_SXGA, // 6: 1280x1024
                                     FRAMESIZE_UXGA}; // 7: 1600x1200

  framesize_t targetResolution = FRAMESIZE_QVGA; // Default
  if (resolutionIndex < 8) {
    targetResolution = resolutions[resolutionIndex];
  }

  // Change camera settings
  if (!changeResolution(targetResolution)) {
    Serial.println("Failed to change resolution");
    if (pCharImageInfo) {
      uint8_t errorInfo[7] = {2, 0, 0, 0, 0, 0, 0}; // Status: 2 = error
      pCharImageInfo->setValue(errorInfo, 7);
      pCharImageInfo->notify();
    }
    return false;
  }

  if (!changeQuality(quality)) {
    Serial.println("Failed to change quality");
  }

  // delay(100); // Allow camera to adjust

  // Discard stale frame from buffer
  camera_fb_t *discard = esp_camera_fb_get();
  if (discard)
    esp_camera_fb_return(discard);

  // Capture image
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    if (pCharImageInfo) {
      uint8_t errorInfo[7] = {2, 0, 0, 0, 0, 0, 0}; // Status: 2 = error
      pCharImageInfo->setValue(errorInfo, 7);
      pCharImageInfo->notify();
    }
    return false;
  }

  Serial.printf("Image captured: %d bytes\n", fb->len);

  // Check if image is too large
  if (fb->len > BLE_IMAGE_MAX_SIZE) {
    Serial.printf("Image too large: %d bytes (max: %d)\n", fb->len, BLE_IMAGE_MAX_SIZE);
    esp_camera_fb_return(fb);
    if (pCharImageInfo) {
      uint8_t errorInfo[7] = {3, 0, 0, 0, 0, 0, 0}; // Status: 3 = too large
      pCharImageInfo->setValue(errorInfo, 7);
      pCharImageInfo->notify();
    }
    return false;
  }

  // Allocate buffer and copy image data
  imageBuffer = static_cast<uint8_t *>(malloc(fb->len));
  if (!imageBuffer) {
    Serial.println("Failed to allocate image buffer");
    esp_camera_fb_return(fb);
    if (pCharImageInfo) {
      uint8_t errorInfo[7] = {2, 0, 0, 0, 0, 0, 0}; // Status: 2 = error
      pCharImageInfo->setValue(errorInfo, 7);
      pCharImageInfo->notify();
    }
    return false;
  }

  memcpy(imageBuffer, fb->buf, fb->len);
  imageSize = fb->len;
  esp_camera_fb_return(fb);

  // Calculate number of chunks
  totalChunks = (imageSize + BLE_IMAGE_CHUNK_SIZE - 1) / BLE_IMAGE_CHUNK_SIZE;
  currentChunk = 0;
  imageTransferActive = true;
  queuedNotifyCount = 0;  // Reset queue counter for new transfer

  Serial.printf("Image prepared: %d bytes, %d chunks\n", imageSize, totalChunks);

  // Update image info characteristic
  // Format: [status, size_low, size_mid_low, size_mid_high, size_high, chunks_low, chunks_high]
  if (pCharImageInfo) {
    uint8_t imageInfo[7];
    imageInfo[0] = 1; // Status: 1 = ready
    imageInfo[1] = imageSize & 0xFF;
    imageInfo[2] = (imageSize >> 8) & 0xFF;
    imageInfo[3] = (imageSize >> 16) & 0xFF;
    imageInfo[4] = (imageSize >> 24) & 0xFF;
    imageInfo[5] = totalChunks & 0xFF;
    imageInfo[6] = (totalChunks >> 8) & 0xFF;
    pCharImageInfo->setValue(imageInfo, 7);
    pCharImageInfo->notify();
    Serial.println("Image info sent via BLE");
  }

  // Automatically send all chunks in batches
  Serial.println("Auto-sending all chunks...");
  for (uint16_t i = 0; i < totalChunks; i += BLE_IMAGE_DATA_CHANNELS) {
    sendImageChunk(i);

    // Print progress every few batches
    if (i % 16 == 0 || i + BLE_IMAGE_DATA_CHANNELS >= totalChunks) {
      uint16_t sent = (i + BLE_IMAGE_DATA_CHANNELS > totalChunks) ? totalChunks : i + BLE_IMAGE_DATA_CHANNELS;
      Serial.printf("Progress: sent up to chunk %d/%d\n", sent, totalChunks);
    }
  }

  // Send transfer complete notification
  if (pCharImageInfo) {
    uint8_t completeInfo[7];
    completeInfo[0] = 4; // Status: 4 = complete
    completeInfo[1] = imageSize & 0xFF;
    completeInfo[2] = (imageSize >> 8) & 0xFF;
    completeInfo[3] = (imageSize >> 16) & 0xFF;
    completeInfo[4] = (imageSize >> 24) & 0xFF;
    completeInfo[5] = totalChunks & 0xFF;
    completeInfo[6] = (totalChunks >> 8) & 0xFF;
    pCharImageInfo->setValue(completeInfo, 7);
    pCharImageInfo->notify();
    Serial.println("BLE: Sent status 4 (initial transfer complete)");
  }

  Serial.println("All chunks sent, awaiting client confirmation...");
  Serial.println("======================================");
  return true;
}

bool BLEImageTransfer::sendImageChunk(const uint16_t chunkIndex, const int count) {
  // Allow sending if buffer exists (supports both auto-send and retransmit)
  if (!imageBuffer) {
    Serial.println("No image buffer available");
    return false;
  }

  if (chunkIndex >= totalChunks) {
    Serial.printf("Invalid chunk index: %d (max: %d)\n", chunkIndex, totalChunks - 1);
    return false;
  }

  // Determine how many chunks to send
  int chunksToSend;
  if (count == -1) {
    // Batch mode: send up to 8 chunks in parallel (one per channel)
    chunksToSend = min(BLE_IMAGE_DATA_CHANNELS, (int) (totalChunks - chunkIndex));
  } else {
    // Single/specified mode: send exact count requested
    chunksToSend = min(count, (int) (totalChunks - chunkIndex));
  }

  for (int i = 0; i < chunksToSend; i++) {
    uint16_t currentChunkIndex = chunkIndex + i;

    // Calculate chunk offset and size
    const size_t chunkOffset = currentChunkIndex * BLE_IMAGE_CHUNK_SIZE;
    size_t chunkSize = BLE_IMAGE_CHUNK_SIZE;
    if (chunkOffset + chunkSize > imageSize) {
      chunkSize = imageSize - chunkOffset;
    }

    // Prepare chunk data: [chunk_index_low, chunk_index_high, ...data...]
    uint8_t chunkData[BLE_IMAGE_CHUNK_SIZE + 2];
    chunkData[0] = currentChunkIndex & 0xFF;
    chunkData[1] = (currentChunkIndex >> 8) & 0xFF;
    memcpy(chunkData + 2, imageBuffer + chunkOffset, chunkSize);

    // Send chunk to corresponding channel
    pCharImageData[i]->setValue(chunkData, chunkSize + 2);
    pCharImageData[i]->notify();

    currentChunk = currentChunkIndex;
  }

  
  // Smart delay based on total image size
  // Larger images (more total chunks) need more aggressive throttling
  int delayMs = 0;
  
  if (totalChunks > 100) {
    // Large image: delay 30 ms
    delayMs = 20;
  } else if (totalChunks > 50) {
    // Medium image: delay 20 ms
    delayMs = 20;
  } else if (totalChunks > 40) {
    delayMs = 0;
  } else {
    // Small image: delay 0 ms
    delayMs = 0;
  }
  
  
  Serial.printf("  [Delay: %dms, total chunks: %d]\n", 
                delayMs, totalChunks);
  delay(delayMs);

  Serial.printf("Sent chunks %d-%d/%d (batch of %d)\n", chunkIndex + 1, chunkIndex + chunksToSend, totalChunks,
                chunksToSend);

  // Note: Buffer is NOT freed here to support retransmit requests
  // It will be freed when a new capture request comes or in cancelImageTransfer()

  return true;
}

void BLEImageTransfer::cancelImageTransfer() {
  if (imageBuffer) {
    free(imageBuffer);
    imageBuffer = nullptr;
  }
  imageSize = 0;
  totalChunks = 0;
  currentChunk = 0;
  imageTransferActive = false;

  // Update status to idle
  if (pCharImageInfo) {
    uint8_t imageInfo[7] = {0}; // Status: 0 = idle
    pCharImageInfo->setValue(imageInfo, 7);
    pCharImageInfo->notify();
  }

  Serial.println("Image transfer cancelled");
}

bool BLEImageTransfer::isImageTransferActive() const { return imageTransferActive; }

String BLEImageTransfer::getDeviceName() const { return String(deviceName); }

void BLEImageTransfer::processPendingRequests() {
  // Process batch chunk retransmit requests FIRST (before new image requests)
  if (hasPendingChunkRequests) {
    hasPendingChunkRequests = false;
    
    if (pendingChunkCount == 0) {
      Serial.println("BLE: No chunks to retransmit");
      return;
    }
    
    if (!imageBuffer) {
      Serial.println("BLE: Cannot retransmit - image buffer already released");
      pendingChunkCount = 0;
      return;
    }
    
    Serial.printf("BLE: Processing batch retransmit - %d chunks\n", pendingChunkCount);
    
    // Send chunks in batches of 8 (parallel channels), just like initial transfer
    uint8_t totalToSend = pendingChunkCount;
    for (uint8_t i = 0; i < totalToSend; i += BLE_IMAGE_DATA_CHANNELS) {
      uint8_t batchSize = min((uint8_t)BLE_IMAGE_DATA_CHANNELS, (uint8_t)(totalToSend - i));
      
      // Send batch in parallel across channels
      for (uint8_t j = 0; j < batchSize; j++) {
        uint16_t chunkIdx = pendingChunkIndexes[i + j];
        
        if (chunkIdx >= totalChunks) {
          Serial.printf("BLE: Invalid chunk index %d (max: %d)\n", chunkIdx, totalChunks - 1);
          continue;
        }
        
        // Calculate chunk offset and size
        const size_t chunkOffset = chunkIdx * BLE_IMAGE_CHUNK_SIZE;
        size_t chunkSize = BLE_IMAGE_CHUNK_SIZE;
        if (chunkOffset + chunkSize > imageSize) {
          chunkSize = imageSize - chunkOffset;
        }
        
        // Prepare chunk data: [chunk_index_low, chunk_index_high, ...data...]
        uint8_t chunkData[BLE_IMAGE_CHUNK_SIZE + 2];
        chunkData[0] = chunkIdx & 0xFF;
        chunkData[1] = (chunkIdx >> 8) & 0xFF;
        memcpy(chunkData + 2, imageBuffer + chunkOffset, chunkSize);
        
        // Send to corresponding channel (parallel)
        pCharImageData[j]->setValue(chunkData, chunkSize + 2);
        pCharImageData[j]->notify();
      }
      
      // Smart delay based on batch size to prevent BLE queue overflow
      if (batchSize > 0) {
        const int delayMs = batchSize * 2;  // 2ms per chunk
        Serial.printf("  [Retransmit delay: %dms for %d chunks]\n", delayMs, batchSize);
        delay(delayMs);
      }
      
      Serial.printf("BLE: Retransmitted batch %u-%u/%u\n", i, i + batchSize - 1, totalToSend - 1);
    }
    
    pendingChunkCount = 0;
    Serial.println("BLE: Batch retransmit complete");
    
    // Send status 4 (transfer complete) after retransmit
    if (pCharImageInfo) {
      uint8_t imageInfo[7];
      imageInfo[0] = 4; // Status: 4 = transfer complete
      imageInfo[1] = imageSize & 0xFF;
      imageInfo[2] = (imageSize >> 8) & 0xFF;
      imageInfo[3] = (imageSize >> 16) & 0xFF;
      imageInfo[4] = (imageSize >> 24) & 0xFF;
      imageInfo[5] = totalChunks & 0xFF;
      imageInfo[6] = (totalChunks >> 8) & 0xFF;
      pCharImageInfo->setValue(imageInfo, 7);
      pCharImageInfo->notify();
      Serial.println("BLE: Sent status 4 (transfer complete) after retransmit");
    }
  }
  
  // Process image capture requests (after retransmit is done)
  if (hasPendingRequest && !imageTransferActive) {
    hasPendingRequest = false;
    Serial.println("BLE: Processing pending image request");
    captureAndPrepareImage(pendingResolutionIndex, pendingQuality);
  }
}

void BLEImageTransfer::cleanup() {
  // Clean up image buffer if exists
  cancelImageTransfer();

  // Clean up callback objects to prevent memory leak
  delete pServerCallbacks;
  delete pImageRequestCallbacks;
  delete pImageControlCallbacks;

  // Reset pointers
  pServerCallbacks = nullptr;
  pImageRequestCallbacks = nullptr;
  pImageControlCallbacks = nullptr;
  pCharImageRequest = nullptr;
  pCharImageInfo = nullptr;
  for (int i = 0; i < BLE_IMAGE_DATA_CHANNELS; i++) {
    pCharImageData[i] = nullptr;
  }
  pCharImageControl = nullptr;

  // Deinitialize BLE
  if (pServer) {
    BLEDevice::deinit(true);
    pServer = nullptr;
    pImageService = nullptr;
    pImageDataService1 = nullptr;
    pImageDataService2 = nullptr;
  }

  Serial.println("BLE Image Transfer: Cleanup complete");
}
