#ifndef UDP_STREAM_H
#define UDP_STREAM_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_camera.h"

// UDP configuration
#define UDP_PORT 5000
#define MAX_PACKET_SIZE 1400  // MTU - headers

// Frame header structure (12 bytes, little-endian)
struct FrameHeader {
    uint32_t frameNumber;     // Frame sequence number
    uint32_t totalSize;       // Total JPEG size in bytes
    uint16_t chunkIndex;      // Chunk index (0-based)
    uint16_t totalChunks;     // Total number of chunks
} __attribute__((packed));

// UDP video stream manager
class UDPStream {
public:
    UDPStream();

    // Initialize UDP socket
    bool begin(uint16_t port = UDP_PORT);

    // Start/stop streaming
    void start(IPAddress clientIP, uint16_t clientPort = UDP_PORT);
    void stop();

    // Check if currently streaming
    bool isStreaming();

    // Send frame (call in main loop)
    void sendFrame();

    // Get statistics
    uint32_t getFramesSent();
    uint64_t getBytesSent();

private:
    WiFiUDP udp;
    IPAddress clientIP;
    uint16_t clientPort;
    uint16_t localPort;  // Dynamic local port

    uint32_t frameNumber;

    bool streaming;
    bool initialized;
};

#endif // UDP_STREAM_H
