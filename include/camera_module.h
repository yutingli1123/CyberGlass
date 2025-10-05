/**
 * @file camera_module.h
 * @brief Camera module interface for XIAO ESP32S3 Sense
 * @author CyberGlass Project
 * @date 2025-10-04
 * 
 * This module provides camera functionality with serial command interface
 * and timestamp support for photo capture operations.
 */

#ifndef CAMERA_MODULE_H
#define CAMERA_MODULE_H

#include <Arduino.h>
#include "esp_camera.h"

/**
 * @brief Initialize the camera module
 * @return true if initialization successful, false otherwise
 */
bool initCamera();

/**
 * @brief Capture a photo and output information via serial
 * @return true if capture successful, false otherwise
 */
bool capturePhoto();

/**
 * @brief Capture a photo and send via serial as Base64 encoded data
 * @return true if capture successful, false otherwise
 */
bool captureAndSendBase64();

/**
 * @brief Change camera resolution
 * @param frameSize Target frame size (e.g., FRAMESIZE_UXGA, FRAMESIZE_SVGA)
 * @return true if resolution changed successfully, false otherwise
 */
bool changeResolution(framesize_t frameSize);

/**
 * @brief Change JPEG quality
 * @param quality JPEG quality (0-63)
 * @return true if quality changed successfully, false otherwise
 */
bool changeQuality(int quality);

/**
 * @brief Start video streaming mode
 * Continuously captures and sends frames until stopped
 */
void startVideoStream();

/**
 * @brief Start video streaming mode with binary transfer (faster)
 * Continuously captures and sends frames as raw binary data
 */
void startBinaryVideoStream();

/**
 * @brief Process serial commands for camera control
 * Commands:
 *   - "capture" or "c": Take a photo and display info
 *   - "send" or "d": Take a photo and send via Base64
 *   - "status" or "s": Show camera status
 *   - "resolution" or "r": Change camera resolution
 *   - "help" or "h": Show available commands
 */
void processCameraCommand();

/**
 * @brief Get current timestamp string
 * @return String containing formatted timestamp
 */
String getTimestamp();

#endif // CAMERA_MODULE_H
