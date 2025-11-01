/**
 * @file base64_encoder.h
 * @brief Base64 encoding utility for image transmission
 * @author CyberGlass Project
 * @date 2025-10-04
 */

#ifndef BASE64_ENCODER_H
#define BASE64_ENCODER_H

#include <Arduino.h>

/**
 * @brief Encode binary data to Base64 and send via serial
 * @param data Pointer to binary data
 * @param length Length of data in bytes
 */
void sendBase64(const uint8_t *data, size_t length);

#endif // BASE64_ENCODER_H
