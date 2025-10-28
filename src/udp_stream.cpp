#include "udp_stream.h"

UDPStream::UDPStream()
    : clientPort(0), localPort(4999), frameNumber(0), streaming(false), initialized(false)
{
}

bool UDPStream::begin(uint16_t port)
{
    Serial.println("=== Initializing UDP Video Stream ===");

    // Start UDP
    if (!udp.begin(port))
    {
        Serial.println("ERROR: Failed to start UDP");
        return false;
    }

    initialized = true;

    Serial.println("UDP stream initialized successfully!");
    Serial.printf("Listening on port: %d\n", port);
    Serial.println("Use HTTP endpoint /start to begin streaming");
    Serial.println("======================================");

    return true;
}

void UDPStream::start(IPAddress ip, uint16_t port)
{
    clientIP = ip;
    clientPort = port;
    frameNumber = 0;

    // Use a new local port each time to avoid port reuse issues
    localPort++;
    if (localPort > 5100)
        localPort = 5000; // Cycle through ports 5000-5100

    // Reinitialize UDP socket with new local port
    udp.begin(localPort);

    streaming = true;

    Serial.printf("UDP: Streaming started to %s:%d (from local port %d)\n",
                  clientIP.toString().c_str(), clientPort, localPort);
}

void UDPStream::stop()
{
    streaming = false;

    // Stop UDP
    udp.stop();

    Serial.println("UDP: Streaming stopped");
}

bool UDPStream::isStreaming()
{
    return streaming;
}

void UDPStream::sendFrame()
{
    if (!initialized || !streaming)
    {
        return;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb || fb->len == 0)
    {
        if (fb)
            esp_camera_fb_return(fb);
        return;
    }

    frameNumber++;

    // Calculate chunks with overflow protection
    size_t chunkPayloadSize = MAX_PACKET_SIZE - sizeof(FrameHeader);
    size_t totalChunks = (fb->len + chunkPayloadSize - 1) / chunkPayloadSize;

    // uint16_t max is 65535 chunks
    if (totalChunks > 65535)
    {
        esp_camera_fb_return(fb);
        return;
    }

    uint16_t totalChunks16 = (uint16_t)totalChunks;

    // Reuse header structure to avoid per-chunk allocation
    FrameHeader header;
    header.frameNumber = frameNumber;
    header.totalSize = fb->len;
    header.totalChunks = totalChunks16;

    for (uint16_t i = 0; i < totalChunks16; i++)
    {
        header.chunkIndex = i;

        size_t offset = i * chunkPayloadSize;
        if (offset >= fb->len)
        {
            break;
        }
        size_t chunkDataSize = min(chunkPayloadSize, fb->len - offset);

        udp.beginPacket(clientIP, clientPort);
        udp.write((uint8_t *)&header, sizeof(header));
        udp.write(fb->buf + offset, chunkDataSize);

        udp.endPacket();
    }

    esp_camera_fb_return(fb);
}
