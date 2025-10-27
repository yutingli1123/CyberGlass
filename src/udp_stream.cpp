#include "udp_stream.h"

UDPStream::UDPStream()
    : clientPort(0), frameNumber(0), framesSent(0), bytesSent(0),
      frameRate(30), frameInterval(33), lastFrameTime(0),
      streaming(false), initialized(false) {
}

bool UDPStream::begin(uint16_t port) {
    Serial.println("=== Initializing UDP Video Stream ===");

    // Start UDP
    if (!udp.begin(port)) {
        Serial.println("ERROR: Failed to start UDP");
        return false;
    }

    initialized = true;

    Serial.println("UDP stream initialized successfully!");
    Serial.printf("Listening on port: %d\n", port);
    Serial.printf("Frame rate: %d FPS (%lu ms/frame)\n", frameRate, frameInterval);
    Serial.println("Use HTTP endpoint /start to begin streaming");
    Serial.println("======================================");

    return true;
}

void UDPStream::start(IPAddress ip, uint16_t port) {
    clientIP = ip;
    clientPort = port;
    streaming = true;
    frameNumber = 0;
    framesSent = 0;

    Serial.printf("UDP: Streaming started to %s:%d\n",
                  clientIP.toString().c_str(), clientPort);
}

void UDPStream::stop() {
    streaming = false;
    Serial.println("UDP: Streaming stopped");
}

bool UDPStream::isStreaming() {
    return streaming;
}

void UDPStream::sendFrame() {
    if (!initialized || !streaming) {
        return;
    }

    // Frame rate control
    unsigned long now = millis();
    if (now - lastFrameTime < frameInterval) {
        return;
    }
    lastFrameTime = now;

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        return;
    }

    // Skip oversized frames (> 100KB)
    if (fb->len > 100000) {
        esp_camera_fb_return(fb);
        return;
    }

    // Skip empty frames
    if (fb->len == 0) {
        esp_camera_fb_return(fb);
        return;
    }

    frameNumber++;

    // Calculate chunks with overflow protection
    size_t chunkPayloadSize = MAX_PACKET_SIZE - sizeof(FrameHeader);
    size_t totalChunks = (fb->len + chunkPayloadSize - 1) / chunkPayloadSize;

    // uint16_t max is 65535 chunks
    if (totalChunks > 65535) {
        esp_camera_fb_return(fb);
        return;
    }

    uint16_t totalChunks16 = (uint16_t)totalChunks;

    // Reuse header structure to avoid per-chunk allocation
    FrameHeader header;
    header.frameNumber = frameNumber;
    header.totalSize = fb->len;
    header.totalChunks = totalChunks16;

    for (uint16_t i = 0; i < totalChunks16; i++) {
        header.chunkIndex = i;

        size_t offset = i * chunkPayloadSize;
        if (offset >= fb->len) {
            break;
        }
        size_t chunkDataSize = min(chunkPayloadSize, fb->len - offset);

        udp.beginPacket(clientIP, clientPort);
        udp.write((uint8_t*)&header, sizeof(header));
        udp.write(fb->buf + offset, chunkDataSize);
        udp.endPacket();

        bytesSent += sizeof(header) + chunkDataSize;

        // Small delay for large frames to prevent WiFi buffer overflow
        if (totalChunks16 > 10) {
            delayMicroseconds(30);
        }
    }

    framesSent++;
    esp_camera_fb_return(fb);
}

void UDPStream::setFrameRate(uint8_t fps) {
    if (fps < 1) fps = 1;
    if (fps > 60) fps = 60;

    frameRate = fps;
    frameInterval = 1000 / fps;

    Serial.printf("UDP: Frame rate changed to %d FPS (%lu ms/frame)\n",
                  frameRate, frameInterval);
}

uint32_t UDPStream::getFramesSent() {
    return framesSent;
}

uint64_t UDPStream::getBytesSent() {
    return bytesSent;
}
