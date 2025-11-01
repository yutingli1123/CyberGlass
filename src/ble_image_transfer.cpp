#include "ble_image_transfer.h"
#include "camera_module.h"
#include "esp_camera.h"

// BLE Callback class for handling image transfer requests
class ImageTransferCallbacks final : public BLECharacteristicCallbacks {
  BLEImageTransfer *transfer;

public:
  explicit ImageTransferCallbacks(BLEImageTransfer *xfer) : transfer(xfer) {}

  void onWrite(BLECharacteristic *pCharacteristic) override {
    const std::string uuid = pCharacteristic->getUUID().toString();
    const std::string value = pCharacteristic->getValue();

    if (uuid == BLE_CHAR_IMAGE_REQUEST_UUID) {
      // Format: [resolution_index, quality]
      if (value.length() >= 2) {
        const uint8_t resolutionIndex = static_cast<uint8_t>(value[0]);
        const uint8_t quality = static_cast<uint8_t>(value[1]);
        Serial.printf("BLE: Image request - Resolution: %d, Quality: %d\n", resolutionIndex, quality);
        transfer->captureAndPrepareImage(resolutionIndex, quality);
      }
    } else if (uuid == BLE_CHAR_IMAGE_CONTROL_UUID) {
      if (!value.empty()) {
        const uint8_t command = static_cast<uint8_t>(value[0]);
        if (command == 0) {
          // Cancel transfer
          Serial.println("BLE: Image transfer cancelled");
          transfer->cancelImageTransfer();
        } else if (command == 1 && value.length() >= 3) {
          // Request specific chunk: [1, chunk_low, chunk_high]
          const uint16_t chunkIndex = static_cast<uint8_t>(value[1]) | (static_cast<uint8_t>(value[2]) << 8);
          Serial.printf("BLE: Chunk %d requested\n", chunkIndex);
          transfer->sendImageChunk(chunkIndex);
        }
      }
    }
  }
};

BLEImageTransfer::BLEImageTransfer() :
    pCharImageRequest(nullptr), pCharImageInfo(nullptr), pCharImageData(nullptr), pCharImageControl(nullptr),
    pImageRequestCallbacks(nullptr), pImageControlCallbacks(nullptr), imageBuffer(nullptr), imageSize(0),
    totalChunks(0), currentChunk(0), imageTransferActive(false) {}

bool BLEImageTransfer::initCharacteristics(BLEServer *pServer, BLEService *pService) {
  if (!pServer || !pService) {
    Serial.println("BLE Image Transfer: Invalid server or service");
    return false;
  }

  Serial.println("=== Initializing BLE Image Transfer ===");
  Serial.printf("Service pointer: %p\n", pService);
  Serial.printf("Server pointer: %p\n", pServer);

  // Create Image Request Characteristic (Write) - Request image capture
  Serial.println("Creating Image Request characteristic...");
  pCharImageRequest = pService->createCharacteristic(BLE_CHAR_IMAGE_REQUEST_UUID, BLECharacteristic::PROPERTY_WRITE);
  Serial.printf("Image Request created: %p\n", pCharImageRequest);
  pImageRequestCallbacks = new ImageTransferCallbacks(this);
  pCharImageRequest->setCallbacks(pImageRequestCallbacks);

  // Create Image Info Characteristic (Read/Notify) - Image metadata
  Serial.println("Creating Image Info characteristic...");
  pCharImageInfo = pService->createCharacteristic(BLE_CHAR_IMAGE_INFO_UUID, BLECharacteristic::PROPERTY_READ |
                                                                                BLECharacteristic::PROPERTY_NOTIFY);
  pCharImageInfo->addDescriptor(new BLE2902());
  const uint8_t initInfo[7] = {0}; // [status, size_low, size_high, size_high2, size_high3, chunks_low, chunks_high]
  pCharImageInfo->setValue(const_cast<uint8_t *>(initInfo), 7);
  Serial.printf("Image Info created: %p\n", pCharImageInfo);

  // Create Image Data Characteristic (Read/Notify) - Image data chunks
  Serial.println("Creating Image Data characteristic...");
  pCharImageData = pService->createCharacteristic(BLE_CHAR_IMAGE_DATA_UUID, BLECharacteristic::PROPERTY_READ |
                                                                                BLECharacteristic::PROPERTY_NOTIFY);
  pCharImageData->addDescriptor(new BLE2902());
  Serial.printf("Image Data created: %p\n", pCharImageData);

  // Create Image Control Characteristic (Write) - Control transfer
  Serial.println("Creating Image Control characteristic...");
  pCharImageControl = pService->createCharacteristic(BLE_CHAR_IMAGE_CONTROL_UUID, BLECharacteristic::PROPERTY_WRITE);
  pImageControlCallbacks = new ImageTransferCallbacks(this);
  pCharImageControl->setCallbacks(pImageControlCallbacks);
  Serial.printf("Image Control created: %p\n", pCharImageControl);

  Serial.println("BLE Image Transfer: ALL 4 characteristics created successfully!");
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

  delay(100); // Allow camera to adjust

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

  // Automatically send first chunk
  sendImageChunk(0);

  Serial.println("======================================");
  return true;
}

bool BLEImageTransfer::sendImageChunk(const uint16_t chunkIndex) {
  if (!imageTransferActive || !imageBuffer || !pCharImageData) {
    Serial.println("No active image transfer");
    return false;
  }

  if (chunkIndex >= totalChunks) {
    Serial.printf("Invalid chunk index: %d (max: %d)\n", chunkIndex, totalChunks - 1);
    return false;
  }

  // Calculate chunk offset and size
  const size_t chunkOffset = chunkIndex * BLE_IMAGE_CHUNK_SIZE;
  size_t chunkSize = BLE_IMAGE_CHUNK_SIZE;
  if (chunkOffset + chunkSize > imageSize) {
    chunkSize = imageSize - chunkOffset;
  }

  // Prepare chunk data: [chunk_index_low, chunk_index_high, ...data...]
  uint8_t chunkData[BLE_IMAGE_CHUNK_SIZE + 2];
  chunkData[0] = chunkIndex & 0xFF;
  chunkData[1] = (chunkIndex >> 8) & 0xFF;
  memcpy(chunkData + 2, imageBuffer + chunkOffset, chunkSize);

  // Send chunk
  pCharImageData->setValue(chunkData, chunkSize + 2);
  pCharImageData->notify();

  currentChunk = chunkIndex;
  Serial.printf("Sent chunk %d/%d (%d bytes)\n", chunkIndex + 1, totalChunks, chunkSize);

  // If this was the last chunk, mark transfer as complete
  if (chunkIndex == totalChunks - 1) {
    Serial.println("Image transfer complete!");
    // Update status to complete
    if (pCharImageInfo) {
      uint8_t imageInfo[7];
      imageInfo[0] = 4; // Status: 4 = complete
      imageInfo[1] = imageSize & 0xFF;
      imageInfo[2] = (imageSize >> 8) & 0xFF;
      imageInfo[3] = (imageSize >> 16) & 0xFF;
      imageInfo[4] = (imageSize >> 24) & 0xFF;
      imageInfo[5] = totalChunks & 0xFF;
      imageInfo[6] = (totalChunks >> 8) & 0xFF;
      pCharImageInfo->setValue(imageInfo, 7);
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

  // Update status to idle
  if (pCharImageInfo) {
    uint8_t imageInfo[7] = {0}; // Status: 0 = idle
    pCharImageInfo->setValue(imageInfo, 7);
    pCharImageInfo->notify();
  }

  Serial.println("Image transfer cancelled");
}

bool BLEImageTransfer::isImageTransferActive() const { return imageTransferActive; }

void BLEImageTransfer::cleanup() {
  // Clean up image buffer if exists
  cancelImageTransfer();

  // Clean up callback objects to prevent memory leak
  delete pImageRequestCallbacks;
  delete pImageControlCallbacks;

  // Reset pointers
  pImageRequestCallbacks = nullptr;
  pImageControlCallbacks = nullptr;
  pCharImageRequest = nullptr;
  pCharImageInfo = nullptr;
  pCharImageData = nullptr;
  pCharImageControl = nullptr;

  Serial.println("BLE Image Transfer: Cleanup complete");
}
