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
#define PWDN_GPIO_NUM (-1)
#define RESET_GPIO_NUM (-1)
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

/**
 * @brief Get camera model name string
 * @param model Camera model
 * @return Camera model name
 */
String getCameraModelName(const camera_model_t model) {
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
  return {buffer};
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
  config.frame_size = FRAMESIZE_VGA; // 640x480 (good balance)
  config.jpeg_quality = 15; // Higher quality for better image clarity
  config.fb_count = 1; // Single buffer for minimal latency
  config.grab_mode = CAMERA_GRAB_LATEST; // Always get latest frame immediately

  esp_err_t err = ESP_FAIL;

  config.xclk_freq_hz = 20000000;
  err = esp_camera_init(&config);

  if (err == ESP_OK) {
    Serial.printf("[%s] Camera initialized successfully \n", getTimestamp().c_str());
  } else if (err == ESP_ERR_NOT_FOUND) {
    Serial.printf("[%s] Camera not found (0x105) \n", getTimestamp().c_str());
    esp_camera_deinit();
  } else {
    Serial.printf("[%s] Init failed with error 0x%X \n", getTimestamp().c_str(), err);
    esp_camera_deinit();
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
  if (s != nullptr) {
    // Detect camera model by PID
    const uint16_t pid = s->id.PID;

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
      s->set_gainceiling(s, static_cast<gainceiling_t>(0)); // 0 to 6
      s->set_bpc(s, 0); // Black pixel correction
      s->set_wpc(s, 1); // White pixel correction
      s->set_raw_gma(s, 1); // Enable gamma correction
      s->set_lenc(s, 1); // Enable lens correction
      s->set_hmirror(s, 1); // Horizontal mirror: 0 = disable, 1 = enable
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
      s->set_denoise(s, 0); // Disabled denoise
      s->set_whitebal(s, 1); // Enable white balance
      s->set_awb_gain(s, 1); // Enable AWB gain
      s->set_wb_mode(s, 0); // Auto white balance
      s->set_exposure_ctrl(s, 1); // Enable AEC
      s->set_aec2(s, 1); // Enable AEC DSP
      s->set_ae_level(s, 0); // Neutral exposure compensation
      s->set_aec_value(s, 300); // Default exposure value
      s->set_gain_ctrl(s, 0); // Enable auto gain
      s->set_agc_gain(s, 5); // Auto gain (value not used in auto mode)
      s->set_gainceiling(s, static_cast<gainceiling_t>(0)); // Gain ceiling (0-6)
      s->set_bpc(s, 1); // Black pixel correction
      s->set_wpc(s, 1); // White pixel correction
      s->set_raw_gma(s, 1); // Enable gamma correction
      s->set_lenc(s, 1); // Enable lens correction
      s->set_hmirror(s, 1); // Horizontal mirror
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
String getResolutionName(const framesize_t frameSize) {
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
bool changeResolution(const framesize_t frameSize) {
  if (!cameraInitialized) {
    Serial.println("[" + getTimestamp() + "] ERROR: Camera not initialized");
    return false;
  }

  // Get camera sensor
  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) {
    Serial.println("[" + getTimestamp() + "] ERROR: Failed to get camera sensor");
    return false;
  }

  // Change frame size
  if (s->set_framesize(s, frameSize) != 0) {
    Serial.println("[" + getTimestamp() + "] ERROR: Failed to set resolution");
    return false;
  }

  Serial.println("[" + getTimestamp() + "] Resolution: " + getResolutionName(frameSize));
  return true;
}

/**
 * @brief Change JPEG quality
 * @param quality JPEG quality (0-63)
 * @return true if successful, false otherwise
 */
bool changeQuality(const int quality) {
  if (!cameraInitialized) {
    Serial.println("[" + getTimestamp() + "] ERROR: Camera not initialized");
    return false;
  }

  // Validate quality range
  if (quality < 0 || quality > 63) {
    Serial.println("[" + getTimestamp() + "] ERROR: Quality must be between 0-63");
    return false;
  }

  // Get camera sensor
  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) {
    Serial.println("[" + getTimestamp() + "] ERROR: Failed to get camera sensor");
    return false;
  }

  // Change JPEG quality
  if (s->set_quality(s, quality) != 0) {
    Serial.println("[" + getTimestamp() + "] ERROR: Failed to set quality");
    return false;
  }

  Serial.printf("[%s] Quality: %d (0=highest, 63=lowest)\n", getTimestamp().c_str(), quality);
  return true;
}

/**
 * @brief Process serial commands for camera control
 */
void processCameraCommand() {
  // Read incoming serial data character by character
  while (Serial.available() > 0) {
    const char inChar = static_cast<char>(Serial.read());

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
        } else if (command == "status" || command == "s") {
          Serial.println("\n=== Camera Status ===");
          Serial.println("[" + getTimestamp() + "] Camera initialized: " + String(cameraInitialized ? "YES" : "NO"));
          if (cameraInitialized) {
            Serial.println("[" + getTimestamp() + "] Camera model: " + getCameraModelName(detectedCamera));
            // Get current resolution
            const sensor_t *s = esp_camera_sensor_get();
            if (s != nullptr) {
              Serial.println("[" + getTimestamp() + "] Current resolution: " + getResolutionName(s->status.framesize));
            }
          } else if (lastInitError.length() > 0) {
            Serial.println("[" + getTimestamp() + "] Last error: " + lastInitError);
            Serial.println("[" + getTimestamp() + "] Hint: Try 'init' command to retry");
          }
          Serial.println("[" + getTimestamp() + "] Total photos taken: " + String(photoCount));
          Serial.println("[" + getTimestamp() + "] Free heap: " + String(ESP.getFreeHeap()) + " bytes");
          Serial.println("[" + getTimestamp() + "] Free PSRAM: " + String(ESP.getFreePsram()) + " bytes");
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
          Serial.println("1. QQVGA (160x120)");
          Serial.println("2. QVGA (320x240)");
          Serial.println("3. VGA (640x480) [DEFAULT]");
          Serial.println("4. SVGA (800x600)");
          Serial.println("5. XGA (1024x768)");
          Serial.println("6. HD (1280x720)");
          Serial.println("7. SXGA (1280x1024)");
          Serial.println("8. UXGA (1600x1200)");
          Serial.println("==============================");
          Serial.println("Enter number (1-8) or resolution name:");
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
        } else if (command == "help" || command == "h") {
          Serial.println("\n=== Available Commands ===");
          Serial.println("  init       (i) - (Re)initialize camera module");
          Serial.println("  resolution (r) - Change camera resolution");
          Serial.println("  quality    (q) - Change JPEG quality (0-63)");
          Serial.println("  status     (s) - Show camera status");
          Serial.println("  help       (h) - Show this help message");
          Serial.println("==========================\n");
        }
        // Handle quality setting (if it's a number between 0-63)
        else {
          // Try to parse as quality number
          const int quality = command.toInt();
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
