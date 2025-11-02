#include "ble_image_transfer.h"
#include "camera_module.h"
#include "esp_camera.h"

// Simple BLE callback for image request
class ImageRequestCallback : public BLECharacteristicCallbacks {
  BLEImageTransfer *transfer;

public:
  explicit ImageRequestCallback(BLEImageTransfer *xfer) : transfer(xfer) {}

  void onWrite(BLECharacteristic *pCharacteristic) override {
    std::string value = pCharacteristic->getValue();

    Serial.println("\n=== BLE Write Received ===");
    Serial.printf("Length: %d bytes\n", value.length());

    if (value.length() >= 2) {
      uint8_t resolution = (uint8_t)value[0];
      uint8_t quality = (uint8_t)value[1];
      Serial.printf("Resolution: %d, Quality: %d\n", resolution, quality);
      Serial.println("========================\n");

      // Capture image
      transfer->captureAndPrepareImage(resolution, quality);
    } else {
      Serial.println("ERROR: Invalid data length");
      Serial.println("========================\n");
    }
  }
};

// Simple BLE callback for chunk request
class ChunkRequestCallback : public BLECharacteristicCallbacks {
  BLEImageTransfer *transfer;

public:
  explicit ChunkRequestCallback(BLEImageTransfer *xfer) : transfer(xfer) {}

  void onWrite(BLECharacteristic *pCharacteristic) override {
    std::string value = pCharacteristic->getValue();

    if (value.length() >= 3) {
      uint8_t cmd = (uint8_t)value[0];
      if (cmd == 1) {
        uint16_t chunk = (uint8_t)value[1] | ((uint8_t)value[2] << 8);
        Serial.printf("Chunk %d requested\n", chunk);
        transfer->sendImageChunk(chunk);
      }
    }
  }
};

BLEImageTransfer::BLEImageTransfer() :
    pCharImageRequest(nullptr), pCharImageInfo(nullptr), pCharImageData(nullptr), pCharImageControl(nullptr),
    pImageRequestCallbacks(nullptr), pImageControlCallbacks(nullptr), imageBuffer(nullptr), imageSize(0),
    totalChunks(0), currentChunk(0), imageTransferActive(false), chunkPayloadSize(BLE_IMAGE_MIN_PAYLOAD),
    negotiatedMTU(23) {}

bool BLEImageTransfer::initCharacteristics(BLEServer *pServer, BLEService *pService) {
  if (!pServer || !pService) {
    Serial.println("ERROR: Invalid server or service");
    return false;
  }

  Serial.println("\n=== Creating BLE Image Characteristics ===");

  // 1. Image Request (WRITE) - client sends capture request here
  pCharImageRequest = pService->createCharacteristic(
      BLE_CHAR_IMAGE_REQUEST_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  pCharImageRequest->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
  pImageRequestCallbacks = new ImageRequestCallback(this);
  pCharImageRequest->setCallbacks(pImageRequestCallbacks);
  Serial.println("✓ Image Request characteristic created");

  // 2. Image Info (READ + NOTIFY) - ESP32 sends image metadata here
  pCharImageInfo = pService->createCharacteristic(
      BLE_CHAR_IMAGE_INFO_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharImageInfo->setAccessPermissions(ESP_GATT_PERM_READ);
  pCharImageInfo->addDescriptor(new BLE2902());
  uint8_t initInfo[7] = {0};
  pCharImageInfo->setValue(initInfo, 7);
  Serial.println("✓ Image Info characteristic created");

  // 3. Image Data (READ + NOTIFY) - ESP32 sends chunks here
  pCharImageData = pService->createCharacteristic(
      BLE_CHAR_IMAGE_DATA_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharImageData->setAccessPermissions(ESP_GATT_PERM_READ);
  pCharImageData->addDescriptor(new BLE2902());
  Serial.println("✓ Image Data characteristic created");

  // 4. Image Control (WRITE) - client requests specific chunks
  pCharImageControl = pService->createCharacteristic(
      BLE_CHAR_IMAGE_CONTROL_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  pCharImageControl->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
  pImageControlCallbacks = new ChunkRequestCallback(this);
  pCharImageControl->setCallbacks(pImageControlCallbacks);
  Serial.println("✓ Image Control characteristic created");

  Serial.println("========================================\n");
  return true;
}

uint16_t BLEImageTransfer::calculatePayloadSize() const {
  uint16_t mtu = negotiatedMTU;

  if (mtu < 23) {
    mtu = 23;
  } else if (mtu > 517) {
    mtu = 517;
  }

  if (mtu <= 5) {
    return BLE_IMAGE_MIN_PAYLOAD;
  }

  uint16_t payload = mtu - 5; // ATT header (3 bytes) + chunk header (2 bytes)
  if (payload < BLE_IMAGE_MIN_PAYLOAD) {
    payload = BLE_IMAGE_MIN_PAYLOAD;
  }
  if (payload > BLE_IMAGE_MAX_CHUNK_SIZE) {
    payload = BLE_IMAGE_MAX_CHUNK_SIZE;
  }

  return payload;
}

bool BLEImageTransfer::captureAndPrepareImage(uint8_t resolutionIndex, uint8_t quality) {
  Serial.println("\n=== Capturing Image ===");

  // Clean up any previous transfer
  cancelImageTransfer();

  // Resolution map
  framesize_t resolutions[] = {
      FRAMESIZE_QQVGA,  // 0: 160x120
      FRAMESIZE_QVGA,   // 1: 320x240
      FRAMESIZE_VGA,    // 2: 640x480
      FRAMESIZE_SVGA,   // 3: 800x600
      FRAMESIZE_XGA,    // 4: 1024x768
      FRAMESIZE_HD,     // 5: 1280x720
      FRAMESIZE_SXGA,   // 6: 1280x1024
      FRAMESIZE_UXGA    // 7: 1600x1200
  };

  framesize_t targetRes = FRAMESIZE_QQVGA;
  if (resolutionIndex < 8) {
    targetRes = resolutions[resolutionIndex];
  }

  // Set camera parameters
  if (!changeResolution(targetRes)) {
    Serial.println("ERROR: Failed to change resolution");
    notifyError(2);
    return false;
  }

  if (!changeQuality(quality)) {
    Serial.println("WARNING: Failed to change quality");
  }

  delay(100);
  yield();

  // Capture
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("ERROR: Camera capture failed");
    notifyError(2);
    return false;
  }

  Serial.printf("Captured: %d bytes\n", fb->len);

  // Check size limit
  if (fb->len > BLE_IMAGE_MAX_SIZE) {
    Serial.printf("ERROR: Image too large (%d > %d)\n", fb->len, BLE_IMAGE_MAX_SIZE);
    esp_camera_fb_return(fb);
    notifyError(3);
    return false;
  }

  // Allocate buffer
  imageBuffer = (uint8_t *)malloc(fb->len);
  if (!imageBuffer) {
    Serial.println("ERROR: Failed to allocate buffer");
    esp_camera_fb_return(fb);
    notifyError(2);
    return false;
  }

  // Copy image data
  memcpy(imageBuffer, fb->buf, fb->len);
  imageSize = fb->len;
  esp_camera_fb_return(fb);

  // Calculate chunks
  chunkPayloadSize = calculatePayloadSize();
  Serial.printf("BLE negotiated MTU: %u, payload per notification: %u bytes\n", negotiatedMTU, chunkPayloadSize);

  totalChunks = (imageSize + chunkPayloadSize - 1) / chunkPayloadSize;
  currentChunk = 0;
  imageTransferActive = true;

  Serial.printf("Ready: %d bytes, %d chunks\n", imageSize, totalChunks);
  Serial.println("======================\n");

  // Send info to client
  if (pCharImageInfo) {
    uint8_t info[7];
    info[0] = 1; // Status: Ready
    info[1] = imageSize & 0xFF;
    info[2] = (imageSize >> 8) & 0xFF;
    info[3] = (imageSize >> 16) & 0xFF;
    info[4] = (imageSize >> 24) & 0xFF;
    info[5] = totalChunks & 0xFF;
    info[6] = (totalChunks >> 8) & 0xFF;
    pCharImageInfo->setValue(info, 7);
    pCharImageInfo->notify();
    Serial.println("Image info sent");
  }

  // Send first chunk automatically
  delay(50);
  sendImageChunk(0);

  return true;
}

bool BLEImageTransfer::sendImageChunk(uint16_t chunkIndex) {
  if (!imageTransferActive || !imageBuffer || !pCharImageData) {
    Serial.println("ERROR: No active transfer");
    return false;
  }

  if (chunkIndex >= totalChunks) {
    Serial.printf("ERROR: Invalid chunk %d (max %d)\n", chunkIndex, totalChunks - 1);
    return false;
  }

  // Calculate chunk
  if (chunkPayloadSize == 0) {
    Serial.println("ERROR: Invalid chunk payload size");
    return false;
  }

  size_t offset = static_cast<size_t>(chunkIndex) * chunkPayloadSize;
  size_t size = chunkPayloadSize;
  if (offset + size > imageSize) {
    size = imageSize - offset;
  }

  // Prepare data: [index_low, index_high, ...data...]
  uint8_t chunkData[BLE_IMAGE_MAX_CHUNK_SIZE + 2];
  chunkData[0] = chunkIndex & 0xFF;
  chunkData[1] = (chunkIndex >> 8) & 0xFF;
  memcpy(chunkData + 2, imageBuffer + offset, size);

  // Send
  pCharImageData->setValue(chunkData, size + 2);
  pCharImageData->notify();

  Serial.printf("Sent chunk %d/%d (%d bytes)\n", chunkIndex + 1, totalChunks, size);

  // Check if complete
  if (chunkIndex == totalChunks - 1) {
    Serial.println("\n✓ Transfer complete!\n");
    if (pCharImageInfo) {
      uint8_t info[7];
      info[0] = 4; // Status: Complete
      info[1] = imageSize & 0xFF;
      info[2] = (imageSize >> 8) & 0xFF;
      info[3] = (imageSize >> 16) & 0xFF;
      info[4] = (imageSize >> 24) & 0xFF;
      info[5] = totalChunks & 0xFF;
      info[6] = (totalChunks >> 8) & 0xFF;
      pCharImageInfo->setValue(info, 7);
      pCharImageInfo->notify();
    }
  }

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

  if (pCharImageInfo) {
    uint8_t info[7] = {0}; // Status: Idle
    pCharImageInfo->setValue(info, 7);
    pCharImageInfo->notify();
  }

  chunkPayloadSize = BLE_IMAGE_MIN_PAYLOAD;
}

void BLEImageTransfer::notifyError(uint8_t errorCode) {
  if (pCharImageInfo) {
    uint8_t info[7] = {0};
    info[0] = errorCode; // 2=error, 3=too large
    pCharImageInfo->setValue(info, 7);
    pCharImageInfo->notify();
  }
}

bool BLEImageTransfer::isImageTransferActive() const {
  return imageTransferActive;
}

void BLEImageTransfer::updateNegotiatedMTU(uint16_t mtu) {
  if (mtu < 23) {
    mtu = 23;
  } else if (mtu > 517) {
    mtu = 517;
  }

  negotiatedMTU = mtu;
  Serial.printf("BLE MTU updated: %u bytes\n", negotiatedMTU);

  if (imageTransferActive) {
    Serial.println("WARNING: MTU changed mid-transfer; new size will apply to next capture");
  }
}

void BLEImageTransfer::cleanup() {
  cancelImageTransfer();

  delete pImageRequestCallbacks;
  delete pImageControlCallbacks;

  pImageRequestCallbacks = nullptr;
  pImageControlCallbacks = nullptr;
  pCharImageRequest = nullptr;
  pCharImageInfo = nullptr;
  pCharImageData = nullptr;
  pCharImageControl = nullptr;

  Serial.println("BLE Image Transfer cleaned up");
}
