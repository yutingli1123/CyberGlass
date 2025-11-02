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
 * @brief Capture a photo and send via serial in binary format
 * @return true if capture successful, false otherwise
 */
bool captureAndSendBinary();

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
 * @brief Start video streaming mode with binary transfer
 * Continuously captures and sends frames as raw binary data
 */
void startBinaryVideoStream();

/**
 * @brief Acquire latest frame buffer from camera
 * @return Pointer to camera frame buffer or nullptr if unavailable
 */
camera_fb_t* acquireFrameBuffer();

/**
 * @brief Return a previously acquired frame buffer to the driver
 * @param fb Frame buffer pointer obtained from acquireFrameBuffer()
 */
void releaseFrameBuffer(camera_fb_t* fb);

/**
 * @brief Check if camera hardware is initialized
 * @return true when camera is ready for capture
 */
bool isCameraReady();

/**
 * @brief Increment internal photo counter and return new value
 * @return Updated photo counter
 */
uint32_t incrementPhotoCount();

/**
 * @brief Get current photo counter without modifying it
 * @return Number of photos captured since boot
 */
uint32_t getPhotoCount();

/**
 * @brief Process serial commands for camera control
 * Commands:
 *   - "capture" or "c": Take a photo and display info
 *   - "send" or "d": Take a photo and send in binary format
 *   - "stream": Start video streaming (binary)
 *   - "stop": Stop video streaming
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
