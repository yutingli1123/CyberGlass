/**
 * @file base64_encoder.cpp
 * @brief Base64 encoding utility for image transmission
 * @author CyberGlass Project
 * @date 2025-10-04
 */

#include "base64_encoder.h"

// Base64 encoding table
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

/**
 * @brief Encode binary data to Base64 and send via serial
 * @param data Pointer to binary data
 * @param length Length of data in bytes
 */
void sendBase64(const uint8_t* data, size_t length) {
  const size_t CHARS_PER_LINE = 76;  // Standard Base64 line length
  size_t char_count = 0;
  
  for (size_t i = 0; i < length; i += 3) {
    uint8_t b1 = data[i];
    uint8_t b2 = (i + 1 < length) ? data[i + 1] : 0;
    uint8_t b3 = (i + 2 < length) ? data[i + 2] : 0;
    
    // Encode 3 bytes to 4 Base64 characters
    Serial.write(base64_chars[b1 >> 2]);
    Serial.write(base64_chars[((b1 & 0x03) << 4) | (b2 >> 4)]);
    
    if (i + 1 < length) {
      Serial.write(base64_chars[((b2 & 0x0F) << 2) | (b3 >> 6)]);
    } else {
      Serial.write('=');  // Padding
    }
    
    if (i + 2 < length) {
      Serial.write(base64_chars[b3 & 0x3F]);
    } else {
      Serial.write('=');  // Padding
    }
    
    char_count += 4;
    
    // Add newline every CHARS_PER_LINE characters for readline() compatibility
    if (char_count >= CHARS_PER_LINE) {
      Serial.write('\n');
      char_count = 0;
      
      // Small delay to prevent buffer overflow
      delay(1);
    }
  }
  
  // Add final newline if needed
  if (char_count > 0) {
    Serial.write('\n');
  }
  
  // Ensure all data is sent
  Serial.flush();
}
