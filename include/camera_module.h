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
 * @brief Process serial commands for camera control
 * Commands:
 *   - "init" or "i": (Re)initialize camera module
 *   - "status" or "s": Show camera status
 *   - "resolution" or "r": Change camera resolution
 *   - "quality" or "q": Change JPEG quality
 *   - "help" or "h": Show available commands
 */
void processCameraCommand();

/**
 * @brief Get current timestamp string
 * @return String containing formatted timestamp
 */
String getTimestamp();

#endif // CAMERA_MODULE_H
