/**
 * @file camera_module.cpp
 * @brief Camera module implementation for XIAO ESP32S3 Sense
 * @author CyberGlass Project
 * @date 2025-10-04
 *
 * Supported cameras:
 * - OV2640 (mjy20ff-f3 tl 2409) - Built-in camera
 * - OV5640 (UICPAL2102AYJ24716853) - External high-resolution camera
 */

#include "camera_module.h"
#include "esp_camera.h"

// XIAO ESP32S3 Sense camera pin configuration
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 10
#define SIOD_GPIO_NUM 40
#define SIOC_GPIO_NUM 39

#define Y9_GPIO_NUM 48
#define Y8_GPIO_NUM 11
#define Y7_GPIO_NUM 12
#define Y6_GPIO_NUM 14
#define Y5_GPIO_NUM 16
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 17
#define Y2_GPIO_NUM 15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM 47
#define PCLK_GPIO_NUM 13

// Internal state variables
static bool cameraInitialized = false;
static uint32_t photoCount = 0;
static camera_model_t detectedCamera = CAMERA_NONE;
static String inputBuffer = ""; // Buffer for serial input
static String lastInitError = ""; // Store last initialization error
static bool isStreaming = false; // Video streaming mode flag

/**
 * @brief Get camera model name string
 * @param model Camera model
 * @return Camera model name
 */
String getCameraModelName(camera_model_t model) {
  switch (model) {
    case CAMERA_OV2640:
      return "OV2640 (mjy20ff-f3)";
    case CAMERA_OV5640:
      return "OV5640 (UICPAL2102)";
    case CAMERA_OV3660:
      return "OV3660";
    case CAMERA_OV7725:
      return "OV7725";
    default:
      return "Unknown";
  }
}

/**
 * @brief Get formatted timestamp string
 * @return String with format "HH:MM:SS.mmm"
 */
String getTimestamp() {
  unsigned long ms = millis();
  unsigned long seconds = ms / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;

  ms %= 1000;
  seconds %= 60;
  minutes %= 60;
  hours %= 24;

  char buffer[16];
  sprintf(buffer, "%02lu:%02lu:%02lu.%03lu", hours, minutes, seconds, ms);
  return String(buffer);
}

/**
 * @brief Initialize camera with appropriate settings
 * @return true if successful, false otherwise
 */
bool initCamera() {
  // Allow re-initialization if previously failed
  if (cameraInitialized) {
    Serial.println("[" + getTimestamp() + "] Camera already initialized");
    return true;
  }

  // Reset error message
  lastInitError = "";

  Serial.println("\n=== Camera Module Initialization ===");
  Serial.println("[" + getTimestamp() + "] Starting camera initialization...");

  // Check PSRAM availability
  if (psramFound()) {
    Serial.println("[" + getTimestamp() + "] PSRAM detected: OK");
  } else {
    Serial.println("[" + getTimestamp() + "] WARNING: PSRAM not found - limited resolution");
  }

  // Configure camera settings
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_location = CAMERA_FB_IN_PSRAM; // Use PSRAM for frame buffer

  // Set resolution - Optimized for fast capture with latest frame
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA; // 640x480 (good balance)
    config.jpeg_quality = 15; // Higher quality for better image clarity
    config.fb_count = 1; // Single buffer for minimal latency
    config.grab_mode = CAMERA_GRAB_LATEST; // Always get latest frame immediately
  } else {
    config.frame_size = FRAMESIZE_VGA; // 640x480
    config.jpeg_quality = 15; // Lower quality without PSRAM
    config.fb_count = 1; // Single buffer when no PSRAM
    config.grab_mode = CAMERA_GRAB_LATEST;
  }

  // Try different XCLK frequencies for better compatibility
  // OV5640 typically works with 10-20MHz, OV2640 with 20MHz
  const uint32_t xclk_freqs[] = {20000000, 16000000, 10000000, 8000000};
  const char *freq_names[] = {"20MHz", "16MHz", "10MHz", "8MHz"};
  esp_err_t err = ESP_FAIL;

  for (int i = 0; i < 4; i++) {
    config.xclk_freq_hz = xclk_freqs[i];
    Serial.printf("[%s] Trying XCLK frequency: %s\n", getTimestamp().c_str(), freq_names[i]);

    err = esp_camera_init(&config);

    if (err == ESP_OK) {
      Serial.printf("[%s] Camera initialized successfully with %s\n", getTimestamp().c_str(), freq_names[i]);
      break;
    } else if (err == ESP_ERR_NOT_FOUND) {
      Serial.printf("[%s] Camera not found (0x105) with %s\n", getTimestamp().c_str(), freq_names[i]);
      // Deinit before retry
      esp_camera_deinit();
      delay(100);
    } else {
      Serial.printf("[%s] Init failed with error 0x%X at %s\n", getTimestamp().c_str(), err, freq_names[i]);
      esp_camera_deinit();
      delay(100);
    }
  }

  if (err != ESP_OK) {
    lastInitError = "Camera init failed with error code 0x" + String(err, HEX);
    Serial.printf("[%s] ERROR: %s\n", getTimestamp().c_str(), lastInitError.c_str());
    Serial.println("[" + getTimestamp() + "] Please check:");
    Serial.println("  - Camera module connection (ribbon cable firmly seated)");
    Serial.println("  - Ribbon cable orientation (blue side facing PCB)");
    Serial.println("  - Camera module compatibility (OV2640/OV5640)");
    Serial.println("  - I2C pins (SDA=40, SCL=39)");
    Serial.println("  - Try restarting the board");
    Serial.println("  - Use 'init' command to retry initialization");
    return false;
  }

  // Get camera sensor for additional configuration
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    // Detect camera model by PID
    uint16_t pid = s->id.PID;

    // Identify camera model
    if (pid == OV2640_PID) {
      detectedCamera = CAMERA_OV2640;
    } else if (pid == OV5640_PID) {
      detectedCamera = CAMERA_OV5640;
    } else if (pid == OV3660_PID) {
      detectedCamera = CAMERA_OV3660;
    } else if (pid == OV7725_PID) {
      detectedCamera = CAMERA_OV7725;
    } else {
      detectedCamera = CAMERA_NONE;
    }

    Serial.printf("[%s] Camera PID: 0x%04X\n", getTimestamp().c_str(), pid);
    Serial.println("[" + getTimestamp() + "] Camera model: " + getCameraModelName(detectedCamera));

    // Apply camera-specific optimizations
    if (detectedCamera == CAMERA_OV2640) {
      // OV2640 (mjy20ff-f3 tl 2409) - Built-in camera
      Serial.println("[" + getTimestamp() + "] Applying OV2640 optimizations...");
      s->set_brightness(s, 0); // -2 to 2
      s->set_contrast(s, 0); // -2 to 2
      s->set_saturation(s, 0); // -2 to 2
      s->set_special_effect(s, 0); // 0-6: No effect, Negative, Grayscale...
      s->set_whitebal(s, 1); // Enable white balance
      s->set_awb_gain(s, 1); // Enable AWB gain
      s->set_wb_mode(s, 0); // 0-4: Auto, Sunny, Cloudy, Office, Home
      s->set_exposure_ctrl(s, 1); // Enable AEC
      s->set_aec2(s, 0); // Disable AEC DSP
      s->set_ae_level(s, 0); // -2 to 2
      s->set_aec_value(s, 300); // 0 to 1200
      s->set_gain_ctrl(s, 1); // Enable AGC
      s->set_agc_gain(s, 0); // 0 to 30
      s->set_gainceiling(s, (gainceiling_t) 0); // 0 to 6
      s->set_bpc(s, 0); // Black pixel correction
      s->set_wpc(s, 1); // White pixel correction
      s->set_raw_gma(s, 1); // Enable gamma correction
      s->set_lenc(s, 1); // Enable lens correction
      s->set_hmirror(s, 0); // Horizontal mirror: 0 = disable, 1 = enable
      s->set_vflip(s, 0); // Vertical flip: 0 = disable, 1 = enable
      s->set_dcw(s, 1); // Enable downsize
      s->set_colorbar(s, 0); // Disable color bar test pattern
    } else if (detectedCamera == CAMERA_OV5640) {
      // OV5640 (UICPAL2102AYJ24716853) - High-resolution camera
      Serial.println("[" + getTimestamp() + "] Applying OV5640 optimizations...");
      s->set_brightness(s, 0); // -2 to 2
      s->set_contrast(s, 0); // -2 to 2
      s->set_saturation(s, 0); // -2 to 2
      s->set_sharpness(s, 0); // OV5640 specific
      s->set_denoise(s, 0); // OV5640 specific
      s->set_whitebal(s, 1); // Enable white balance
      s->set_awb_gain(s, 1); // Enable AWB gain
      s->set_wb_mode(s, 0); // Auto white balance
      s->set_exposure_ctrl(s, 1); // Enable AEC
      s->set_aec2(s, 1); // Enable AEC DSP
      s->set_ae_level(s, 0); // -2 to 2
      s->set_aec_value(s, 300); // 0 to 1200
      s->set_gain_ctrl(s, 1); // Enable AGC
      s->set_agc_gain(s, 0); // 0 to 30
      s->set_gainceiling(s, (gainceiling_t) 0); // 0 to 6
      s->set_bpc(s, 1); // Black pixel correction
      s->set_wpc(s, 1); // White pixel correction
      s->set_raw_gma(s, 1); // Enable gamma correction
      s->set_lenc(s, 1); // Enable lens correction
      s->set_hmirror(s, 0); // Horizontal mirror
      s->set_vflip(s, 0); // Vertical flip
      s->set_dcw(s, 1); // Enable downsize
      s->set_colorbar(s, 0); // Disable color bar
    } else {
      // Generic settings for other cameras
      Serial.println("[" + getTimestamp() + "] Applying generic camera settings...");
      s->set_whitebal(s, 1); // Enable white balance
      s->set_awb_gain(s, 1); // Enable AWB gain
      s->set_exposure_ctrl(s, 1); // Enable AEC
      s->set_gain_ctrl(s, 1); // Enable AGC
    }

    Serial.println("[" + getTimestamp() + "] Sensor configuration: OK");
  }

  cameraInitialized = true;
  Serial.println("[" + getTimestamp() + "] Camera initialization: SUCCESS");
  Serial.println("====================================\n");

  return true;
}

/**
 * @brief Get resolution name string
 * @param frameSize Frame size
 * @return Resolution name with dimensions
 */
String getResolutionName(framesize_t frameSize) {
  switch (frameSize) {
    case FRAMESIZE_QQVGA:
      return "QQVGA (160x120)";
    case FRAMESIZE_QCIF:
      return "QCIF (176x144)";
    case FRAMESIZE_HQVGA:
      return "HQVGA (240x176)";
    case FRAMESIZE_QVGA:
      return "QVGA (320x240)";
    case FRAMESIZE_CIF:
      return "CIF (400x296)";
    case FRAMESIZE_VGA:
      return "VGA (640x480)";
    case FRAMESIZE_SVGA:
      return "SVGA (800x600)";
    case FRAMESIZE_XGA:
      return "XGA (1024x768)";
    case FRAMESIZE_SXGA:
      return "SXGA (1280x1024)";
    case FRAMESIZE_UXGA:
      return "UXGA (1600x1200)";
    case FRAMESIZE_HD:
      return "HD (1280x720)";
    case FRAMESIZE_FHD:
      return "FHD (1920x1080)";
    case FRAMESIZE_QXGA:
      return "QXGA (2048x1536)";
    default:
      return "Unknown";
  }
}

/**
 * @brief Change camera resolution
 * @param frameSize Target frame size
 * @return true if successful, false otherwise
 */
bool changeResolution(framesize_t frameSize) {
  if (!cameraInitialized) {
    Serial.println("[" + getTimestamp() + "] ERROR: Camera not initialized");
    return false;
  }

  Serial.println("\n--- Resolution Change Start ---");
  Serial.println("[" + getTimestamp() + "] Changing resolution to: " + getResolutionName(frameSize));

  // Get camera sensor
  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    Serial.println("[" + getTimestamp() + "] ERROR: Failed to get camera sensor");
    return false;
  }

  // Check if resolution is supported
  // Note: Some resolutions require PSRAM
  bool requiresPSRAM = (frameSize >= FRAMESIZE_SXGA);
  if (requiresPSRAM && !psramFound()) {
    Serial.println("[" + getTimestamp() + "] ERROR: This resolution requires PSRAM");
    Serial.println("[" + getTimestamp() + "] Available resolutions without PSRAM:");
    Serial.println("  - QQVGA (160x120)");
    Serial.println("  - QVGA (320x240)");
    Serial.println("  - VGA (640x480)");
    Serial.println("  - SVGA (800x600)");
    return false;
  }

  // Change frame size
  if (s->set_framesize(s, frameSize) != 0) {
    Serial.println("[" + getTimestamp() + "] ERROR: Failed to set resolution");
    return false;
  }

  Serial.println("[" + getTimestamp() + "] Resolution changed: SUCCESS");
  Serial.println("[" + getTimestamp() + "] Current resolution: " + getResolutionName(frameSize));
  Serial.println("--- Resolution Change End ---\n");

  return true;
}

/**
 * @brief Change JPEG quality
 * @param quality JPEG quality (0-63)
 * @return true if successful, false otherwise
 */
bool changeQuality(int quality) {
  if (!cameraInitialized) {
    Serial.println("[" + getTimestamp() + "] ERROR: Camera not initialized");
    return false;
  }

  // Validate quality range
  if (quality < 0 || quality > 63) {
    Serial.println("[" + getTimestamp() + "] ERROR: Quality must be between 0-63");
    return false;
  }

  Serial.println("\n--- JPEG Quality Change Start ---");
  Serial.printf("[%s] Changing JPEG quality to: %d\n", getTimestamp().c_str(), quality);

  // Get camera sensor
  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    Serial.println("[" + getTimestamp() + "] ERROR: Failed to get camera sensor");
    return false;
  }

  // Change JPEG quality
  if (s->set_quality(s, quality) != 0) {
    Serial.println("[" + getTimestamp() + "] ERROR: Failed to set quality");
    return false;
  }

  Serial.println("[" + getTimestamp() + "] JPEG quality changed: SUCCESS");
  Serial.printf("[%s] Current quality: %d (0=highest, 63=lowest)\n", getTimestamp().c_str(), quality);
  Serial.println("--- JPEG Quality Change End ---\n");

  return true;
}

/**
 * @brief Capture a photo and display information
 * @return true if successful, false otherwise
 */
bool capturePhoto() {
  if (!cameraInitialized) {
    Serial.println("[" + getTimestamp() + "] ERROR: Camera not initialized");
    return false;
  }

  Serial.println("\n--- Photo Capture Start ---");
  Serial.println("[" + getTimestamp() + "] Capturing photo...");

  // Get current frame from buffer (camera continuously captures)
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[" + getTimestamp() + "] ERROR: Camera capture failed");
    return false;
  }

  photoCount++;

  // Display photo information
  Serial.println("[" + getTimestamp() + "] Photo capture: SUCCESS");
  Serial.printf("[%s] Photo #%u Information:\n", getTimestamp().c_str(), photoCount);
  Serial.printf("  - Size: %u bytes\n", fb->len);
  Serial.printf("  - Width: %d pixels\n", fb->width);
  Serial.printf("  - Height: %d pixels\n", fb->height);
  Serial.printf("  - Format: %s\n", fb->format == PIXFORMAT_JPEG ? "JPEG" : "RAW");

  // Here you can add code to:
  // - Save to SD card
  // - Send via WiFi/BLE
  // - Process the image

  // Release frame buffer
  esp_camera_fb_return(fb);
  Serial.println("[" + getTimestamp() + "] Frame buffer released");
  Serial.println("--- Photo Capture End ---\n");

  return true;
}


/**
 * @brief Start video streaming mode with binary transfer (faster)
 */
void startBinaryVideoStream() {
  if (!cameraInitialized) {
    Serial.println("[" + getTimestamp() + "] ERROR: Camera not initialized");
    return;
  }

  Serial.println("\n=== Binary Video Streaming Mode ===");
  Serial.println("[" + getTimestamp() + "] Starting binary video stream...");
  Serial.println("[" + getTimestamp() + "] Send 'stop' command to exit");
  Serial.println("==============================\n");

  isStreaming = true;
  unsigned long frameCount = 0;
  unsigned long totalBytes = 0;
  unsigned long streamStart = millis();

  // Small delay to let Python script prepare
  delay(500);

  while (isStreaming) {
    // Check for stop command (non-blocking)
    if (Serial.available() > 0) {
      String cmd = Serial.readStringUntil('\n');
      cmd.trim();
      cmd.toLowerCase();
      if (cmd == "stop") {
        isStreaming = false;
        break;
      }
    }

    // Get frame from buffer
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      // Send error marker
      Serial.write(0xFF);
      Serial.write(0xFF);
      Serial.write(0xFF);
      Serial.write(0xFF);
      delay(100);
      continue;
    }

    frameCount++;
    totalBytes += fb->len;

    // Send frame in binary format:
    // [MARKER: 4 bytes 0xC0 0x1D 0xF1 0x8E - "COLD FIRE"]
    // [SIZE: 4 bytes, little-endian]
    // [DATA: SIZE bytes]
    Serial.write(0xC0);
    Serial.write(0x1D);
    Serial.write(0xF1);
    Serial.write(0x8E);

    // Send size (32-bit little-endian)
    Serial.write((uint8_t) (fb->len & 0xFF));
    Serial.write((uint8_t) ((fb->len >> 8) & 0xFF));
    Serial.write((uint8_t) ((fb->len >> 16) & 0xFF));
    Serial.write((uint8_t) ((fb->len >> 24) & 0xFF));

    // Send JPEG data
    Serial.write(fb->buf, fb->len);
    Serial.flush();

    // Release frame buffer
    esp_camera_fb_return(fb);

    // Small delay for stability
    delay(30); // ~33 FPS max
  }

  // Send end marker
  Serial.write(0xFF);
  Serial.write(0xFF);
  Serial.write(0xFF);
  Serial.write(0xFF);
  Serial.flush();

  // Print streaming statistics
  unsigned long streamDuration = millis() - streamStart;
  Serial.println("\n=== Streaming Statistics ===");
  Serial.printf("[%s] Total frames: %lu\n", getTimestamp().c_str(), frameCount);
  Serial.printf("[%s] Total data: %.2f MB\n", getTimestamp().c_str(), totalBytes / 1048576.0);
  Serial.printf("[%s] Duration: %.1f seconds\n", getTimestamp().c_str(), streamDuration / 1000.0);
  if (streamDuration > 0) {
    Serial.printf("[%s] Average FPS: %.2f\n", getTimestamp().c_str(), frameCount * 1000.0 / streamDuration);
  }
  Serial.println("============================\n");
}

/**
 * @brief Capture a photo and send via serial in binary format
 * @return true if successful, false otherwise
 */
bool captureAndSendBinary() {
  if (!cameraInitialized) {
    Serial.println("[" + getTimestamp() + "] ERROR: Camera not initialized");
    return false;
  }

  Serial.println("\n--- Photo Capture & Send Start ---");
  Serial.println("[" + getTimestamp() + "] Retrieving frame from buffer...");

  // Get current frame from buffer (camera continuously captures)
  // Note: With CAMERA_GRAB_LATEST mode, this returns the latest frame immediately
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[" + getTimestamp() + "] ERROR: Camera capture failed");
    return false;
  }

  photoCount++;

  // Display photo information
  Serial.println("[" + getTimestamp() + "] Photo retrieved: SUCCESS");
  Serial.printf("[%s] Photo #%u Information:\n", getTimestamp().c_str(), photoCount);
  Serial.printf("  - Size: %u bytes (%.2f KB)\n", fb->len, fb->len / 1024.0);
  Serial.printf("  - Width: %d pixels\n", fb->width);
  Serial.printf("  - Height: %d pixels\n", fb->height);
  Serial.printf("  - Format: %s\n", fb->format == PIXFORMAT_JPEG ? "JPEG" : "RAW");

  // Send photo data in binary format
  Serial.println("[" + getTimestamp() + "] Sending photo data in binary format...");

  unsigned long transfer_start = millis();

  // Send frame in binary format:
  // [MARKER: 4 bytes 0xC0 0x1D 0xF1 0x8E - "COLD FIRE"]
  // [SIZE: 4 bytes, little-endian]
  // [DATA: SIZE bytes]
  Serial.write(0xC0);
  Serial.write(0x1D);
  Serial.write(0xF1);
  Serial.write(0x8E);

  // Send size (32-bit little-endian)
  Serial.write((uint8_t) (fb->len & 0xFF));
  Serial.write((uint8_t) ((fb->len >> 8) & 0xFF));
  Serial.write((uint8_t) ((fb->len >> 16) & 0xFF));
  Serial.write((uint8_t) ((fb->len >> 24) & 0xFF));

  // Send JPEG data
  Serial.write(fb->buf, fb->len);
  Serial.flush();

  unsigned long transfer_end = millis();
  unsigned long transfer_time = transfer_end - transfer_start;

  Serial.printf("[%s] Photo transmission: COMPLETE\n", getTimestamp().c_str());
  Serial.printf("[%s] Performance Summary:\n", getTimestamp().c_str());
  Serial.printf("  - Transfer time: %lu ms\n", transfer_time);
  Serial.printf("  - Transfer speed: %.2f KB/s\n", (fb->len / 1024.0) / (transfer_time / 1000.0));
  Serial.printf("  - Note: Frame retrieved from continuous buffer (0ms retrieval)\n");
  // Release frame buffer
  esp_camera_fb_return(fb);
  Serial.println("[" + getTimestamp() + "] Frame buffer released");
  Serial.println("--- Photo Capture & Send End ---\n");

  return true;
}

/**
 * @brief Process serial commands for camera control
 */
void processCameraCommand() {
  // Read incoming serial data character by character
  while (Serial.available() > 0) {
    char inChar = (char) Serial.read();

    // Check for newline character (Enter key)
    if (inChar == '\n' || inChar == '\r') {
      // Only process if buffer is not empty
      if (inputBuffer.length() > 0) {
        // Process the command
        String command = inputBuffer;
        command.trim();
        command.toLowerCase();

        // Clear buffer for next command
        inputBuffer = "";

        // Skip if command is empty after trimming
        if (command.length() == 0) {
          continue;
        }

        Serial.println("\n[" + getTimestamp() + "] Command received: " + command);

        if (command == "init" || command == "i") {
          // Force re-initialization
          cameraInitialized = false;
          if (initCamera()) {
            Serial.println("[" + getTimestamp() + "] Camera re-initialization: SUCCESS");
          } else {
            Serial.println("[" + getTimestamp() + "] Camera re-initialization: FAILED");
          }
        } else if (command == "capture" || command == "c") {
          capturePhoto();
        } else if (command == "send" || command == "d") {
          captureAndSendBinary();
        } else if (command == "stream") {
          startBinaryVideoStream();
        } else if (command == "stop") {
          if (isStreaming) {
            isStreaming = false;
            Serial.println("[" + getTimestamp() + "] Stopping video stream...");
          } else {
            Serial.println("[" + getTimestamp() + "] No active stream to stop");
          }
        } else if (command == "status" || command == "s") {
          Serial.println("\n=== Camera Status ===");
          Serial.println("[" + getTimestamp() + "] Camera initialized: " + String(cameraInitialized ? "YES" : "NO"));
          if (cameraInitialized) {
            Serial.println("[" + getTimestamp() + "] Camera model: " + getCameraModelName(detectedCamera));
            // Get current resolution
            sensor_t *s = esp_camera_sensor_get();
            if (s != NULL) {
              Serial.println("[" + getTimestamp() + "] Current resolution: " + getResolutionName(s->status.framesize));
            }
          } else if (lastInitError.length() > 0) {
            Serial.println("[" + getTimestamp() + "] Last error: " + lastInitError);
            Serial.println("[" + getTimestamp() + "] Hint: Try 'init' command to retry");
          }
          Serial.println("[" + getTimestamp() + "] Total photos taken: " + String(photoCount));
          Serial.println("[" + getTimestamp() + "] PSRAM available: " + String(psramFound() ? "YES" : "NO"));
          Serial.println("[" + getTimestamp() + "] Free heap: " + String(ESP.getFreeHeap()) + " bytes");
          if (psramFound()) {
            Serial.println("[" + getTimestamp() + "] Free PSRAM: " + String(ESP.getFreePsram()) + " bytes");
          }
          Serial.println("====================\n");
        } else if (command == "quality" || command == "q") {
          // Show quality menu
          Serial.println("\n=== JPEG Quality Settings ===");
          Serial.println("Current range: 0-63");
          Serial.println("0  = Highest quality (largest file)");
          Serial.println("10 = High quality (recommended)");
          Serial.println("20 = Medium quality");
          Serial.println("40 = Low quality");
          Serial.println("63 = Lowest quality (smallest file)");
          Serial.println("==============================");
          Serial.println("Enter a number (0-63):");
        } else if (command == "resolution" || command == "r") {
          // Show resolution menu
          Serial.println("\n=== Available Resolutions ===");
          if (psramFound()) {
            Serial.println("1. QQVGA (160x120)");
            Serial.println("2. QVGA (320x240)");
            Serial.println("3. VGA (640x480) [DEFAULT]");
            Serial.println("4. SVGA (800x600)");
            Serial.println("5. XGA (1024x768)");
            Serial.println("6. HD (1280x720)");
            Serial.println("7. SXGA (1280x1024)");
            Serial.println("8. UXGA (1600x1200)");
            Serial.println("9. FHD (1920x1080)");
            Serial.println("0. QXGA (2048x1536)");
          } else {
            Serial.println("1. QQVGA (160x120)");
            Serial.println("2. QVGA (320x240)");
            Serial.println("3. VGA (640x480)");
            Serial.println("4. SVGA (800x600) [DEFAULT]");
            Serial.println("Note: Higher resolutions require PSRAM");
          }
          Serial.println("==============================");
          Serial.println("Enter number (1-9/0) or resolution name:");
        }
        // Handle resolution selection by number
        else if (command == "1") {
          changeResolution(FRAMESIZE_QQVGA);
        } else if (command == "2") {
          changeResolution(FRAMESIZE_QVGA);
        } else if (command == "3") {
          changeResolution(FRAMESIZE_VGA);
        } else if (command == "4") {
          changeResolution(FRAMESIZE_SVGA);
        } else if (command == "5") {
          changeResolution(FRAMESIZE_XGA);
        } else if (command == "6") {
          changeResolution(FRAMESIZE_HD);
        } else if (command == "7") {
          changeResolution(FRAMESIZE_SXGA);
        } else if (command == "8") {
          changeResolution(FRAMESIZE_UXGA);
        } else if (command == "9") {
          changeResolution(FRAMESIZE_FHD);
        } else if (command == "0") {
          changeResolution(FRAMESIZE_QXGA);
        }
        // Handle resolution selection by name
        else if (command == "qqvga") {
          changeResolution(FRAMESIZE_QQVGA);
        } else if (command == "qvga") {
          changeResolution(FRAMESIZE_QVGA);
        } else if (command == "vga") {
          changeResolution(FRAMESIZE_VGA);
        } else if (command == "svga") {
          changeResolution(FRAMESIZE_SVGA);
        } else if (command == "xga") {
          changeResolution(FRAMESIZE_XGA);
        } else if (command == "hd") {
          changeResolution(FRAMESIZE_HD);
        } else if (command == "sxga") {
          changeResolution(FRAMESIZE_SXGA);
        } else if (command == "uxga") {
          changeResolution(FRAMESIZE_UXGA);
        } else if (command == "fhd") {
          changeResolution(FRAMESIZE_FHD);
        } else if (command == "qxga") {
          changeResolution(FRAMESIZE_QXGA);
        } else if (command == "help" || command == "h") {
          Serial.println("\n=== Available Commands ===");
          Serial.println("  init       (i) - (Re)initialize camera module");
          Serial.println("  capture    (c) - Capture a photo and display info");
          Serial.println("  send       (d) - Capture and send photo (binary)");
          Serial.println("  stream         - Start video streaming (binary)");
          Serial.println("  stop           - Stop video streaming");
          Serial.println("  resolution (r) - Change camera resolution");
          Serial.println("  quality    (q) - Change JPEG quality (0-63)");
          Serial.println("  status     (s) - Show camera status");
          Serial.println("  help       (h) - Show this help message");
          Serial.println("==========================\n");
        }
        // Handle quality setting (if it's a number between 0-63)
        else {
          // Try to parse as quality number
          int quality = command.toInt();
          if (quality >= 0 && quality <= 63 && (command == "0" || quality > 0)) {
            changeQuality(quality);
          } else {
            Serial.println("[" + getTimestamp() + "] ERROR: Unknown command '" + command + "'");
            Serial.println("Type 'help' or 'h' for available commands");
          }
        }
      } // End of if (inputBuffer.length() > 0)
    }
    // Add printable characters to buffer (ignore other control characters)
    else if (isPrintable(inChar)) {
      inputBuffer += inChar;

      // Optional: Echo the character back (for better user experience)
      // Serial.print(inChar);

      // Prevent buffer overflow
      if (inputBuffer.length() > 100) {
        inputBuffer = "";
        Serial.println("\n[" + getTimestamp() + "] ERROR: Input too long, buffer cleared");
      }
    }
  }
}
