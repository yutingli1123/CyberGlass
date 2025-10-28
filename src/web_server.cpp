#include "web_server.h"

WebServerManager::WebServerManager(AsyncWebServer *serverPtr, WiFiProvisioning *wifiPtr, UDPStream *udpPtr)
    : server(serverPtr), wifi(wifiPtr), udpStream(udpPtr), lastCaptureTime(0)
{
    Serial.println("Web server manager initialized");
}

void WebServerManager::setupRoutes()
{
    // Capture single photo (JPEG)
    server->on("/capture", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleCapture(request); });

    // Start UDP video stream
    server->on("/stream/start", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleStart(request); });

    // Stop UDP video stream
    server->on("/stream/stop", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleStop(request); });

    // Change resolution
    server->on("/resolution", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleResolution(request); });

    // Change quality
    server->on("/quality", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleQuality(request); });

    // Status endpoint (JSON)
    server->on("/status", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleStatus(request); });

    Serial.println("API endpoints configured:");
    Serial.println("  GET /capture - Capture single photo");
    Serial.println("  GET /stream/start - Start UDP video stream");
    Serial.println("  GET /stream/stop - Stop UDP video stream");
    Serial.println("  GET /resolution?value=<res> - Change resolution");
    Serial.println("  GET /quality?value=<num> - Change quality");
    Serial.println("  GET /status - Get device status (JSON)");
}

void WebServerManager::handleCapture(AsyncWebServerRequest *request)
{
    // Rate limiting: prevent concurrent requests that could exhaust frame buffers
    unsigned long now = millis();
    if (now - lastCaptureTime < MIN_CAPTURE_INTERVAL)
    {
        request->send(429, "text/plain", "Too many requests");
        return;
    }

    // Check PSRAM availability before burst capture
    size_t freePsram = ESP.getFreePsram();
    if (freePsram < MIN_FREE_PSRAM * 2)
    {
        Serial.printf("Capture: Insufficient PSRAM - %d bytes free\n", freePsram);
        request->send(503, "text/plain", "Low memory");
        return;
    }

    lastCaptureTime = now;

    const int NUM_SHOTS = 5;
    Serial.println("Capture: Starting 5-shot burst...");

    // Discard stale frame from buffer
    camera_fb_t* discard = esp_camera_fb_get();
    if (discard) esp_camera_fb_return(discard);

    // With fb_count=1, must copy each frame to heap immediately
    struct FrameData {
        uint8_t* data;
        size_t len;
    };
    FrameData frames[NUM_SHOTS];
    int successCount = 0;

    // Capture and copy each frame immediately
    for (int i = 0; i < NUM_SHOTS; i++)
    {
        // Feed watchdog to prevent timeout during long capture
        yield();  // Let other tasks run

        unsigned long capture_start = millis();
        camera_fb_t *fb = esp_camera_fb_get();
        unsigned long capture_time = millis() - capture_start;

        if (!fb)
        {
            Serial.printf("Capture: [Shot %d/%d] FAILED - fb=null (took %lu ms)\n",
                          i + 1, NUM_SHOTS, capture_time);
            frames[i].data = nullptr;
            frames[i].len = 0;
        }
        else
        {
            Serial.printf("Capture: [Shot %d/%d] SUCCESS - %d bytes, timestamp=%lu.%06lu, took %lu ms\n",
                          i + 1, NUM_SHOTS, fb->len,
                          fb->timestamp.tv_sec, fb->timestamp.tv_usec, capture_time);

            // Copy to heap immediately
            uint8_t* copy = (uint8_t*)malloc(fb->len);
            if (!copy)
            {
                Serial.printf("Capture: [Shot %d/%d] MALLOC FAILED\n", i + 1, NUM_SHOTS);
                esp_camera_fb_return(fb);
                frames[i].data = nullptr;
                frames[i].len = 0;
            }
            else
            {
                memcpy(copy, fb->buf, fb->len);
                frames[i].data = copy;
                frames[i].len = fb->len;
                successCount++;

                // Release camera buffer immediately for next capture
                esp_camera_fb_return(fb);
            }
        }
    }

    if (successCount == 0)
    {
        Serial.println("Capture: All 5 shots failed");
        request->send(500, "text/plain", "Camera failed");
        return;
    }

    Serial.printf("Capture: Captured %d/%d frames successfully\n", successCount, NUM_SHOTS);

    // Calculate total size for multipart response
    size_t totalSize = 0;
    for (int i = 0; i < NUM_SHOTS; i++)
    {
        if (frames[i].data)
        {
            totalSize += frames[i].len;
        }
    }

    String boundary = "CyberGlassBoundary";
    size_t boundaryOverhead = successCount * 300;
    size_t bufferSize = totalSize + boundaryOverhead;

    // Build final multipart buffer
    uint8_t *buffer = (uint8_t *)malloc(bufferSize);
    if (!buffer)
    {
        Serial.println("Capture: Final buffer allocation failed");
        for (int i = 0; i < NUM_SHOTS; i++)
        {
            if (frames[i].data)
                free(frames[i].data);
        }
        request->send(500, "text/plain", "Out of memory");
        return;
    }

    // Build multipart response
    size_t bufferPos = 0;

    for (int i = 0; i < NUM_SHOTS; i++)
    {
        yield();  // Feed watchdog during multipart construction

        if (!frames[i].data)
            continue;

        size_t segmentStart = bufferPos;

        // Add boundary and headers
        String header = "--" + boundary + "\r\n";
        header += "Content-Type: image/jpeg\r\n";
        header += "X-Frame-Index: " + String(i) + "\r\n";
        header += "\r\n"; // End of headers (double CRLF)

        memcpy(buffer + bufferPos, header.c_str(), header.length());
        bufferPos += header.length();

        // Add JPEG data
        memcpy(buffer + bufferPos, frames[i].data, frames[i].len);
        bufferPos += frames[i].len;

        // Add CRLF
        buffer[bufferPos++] = '\r';
        buffer[bufferPos++] = '\n';

        Serial.printf("  Frame %d: jpeg=%d bytes, buffer_pos=%d\n",
                      i, frames[i].len, bufferPos);

        // Free the copied frame
        free(frames[i].data);
    }

    // Add final boundary
    String footer = "--" + boundary + "--\r\n";
    memcpy(buffer + bufferPos, footer.c_str(), footer.length());
    bufferPos += footer.length();

    // Send multipart response
    AsyncWebServerResponse *response = request->beginResponse(
        200,
        "multipart/form-data; boundary=" + boundary,
        buffer,
        bufferPos);

    request->send(response);
}

void WebServerManager::handleResolution(AsyncWebServerRequest *request)
{
    if (request->hasParam("value"))
    {
        String res = request->getParam("value")->value();
        framesize_t size = FRAMESIZE_VGA;

        if (res == "QQVGA")
            size = FRAMESIZE_QQVGA;
        else if (res == "QVGA")
            size = FRAMESIZE_QVGA;
        else if (res == "VGA")
            size = FRAMESIZE_VGA;
        else if (res == "SVGA")
            size = FRAMESIZE_SVGA;
        else if (res == "XGA")
            size = FRAMESIZE_XGA;
        else if (res == "HD")
            size = FRAMESIZE_HD;
        else if (res == "SXGA")
            size = FRAMESIZE_SXGA;
        else if (res == "UXGA")
            size = FRAMESIZE_UXGA;

        changeResolution(size);
        request->send(200, "text/plain", "Resolution changed to " + res);
    }
    else
    {
        request->send(400, "text/plain", "Missing value parameter");
    }
}

void WebServerManager::handleQuality(AsyncWebServerRequest *request)
{
    if (request->hasParam("value"))
    {
        int quality = request->getParam("value")->value().toInt();
        changeQuality(quality);
        request->send(200, "text/plain", "Quality changed to " + String(quality));
    }
    else
    {
        request->send(400, "text/plain", "Missing value parameter");
    }
}

void WebServerManager::handleStart(AsyncWebServerRequest *request)
{
    IPAddress clientIP = request->client()->remoteIP();

    // Get client port from URL parameter, default to UDP_PORT if not specified
    uint16_t clientPort = UDP_PORT;
    if (request->hasParam("port"))
    {
        clientPort = request->getParam("port")->value().toInt();
    }

    udpStream->start(clientIP, clientPort);

    Serial.printf("Start: Streaming to %s:%d\n", clientIP.toString().c_str(), clientPort);

    String response = "{";
    response += "\"status\":\"streaming\",";
    response += "\"client\":\"" + clientIP.toString() + "\",";
    response += "\"port\":" + String(clientPort);
    response += "}";
    request->send(200, "application/json", response);
}

void WebServerManager::handleStop(AsyncWebServerRequest *request)
{
    udpStream->stop();
    Serial.println("Stop: Streaming stopped");
    request->send(200, "application/json", "{\"status\":\"stopped\"}");
}

void WebServerManager::handleStatus(AsyncWebServerRequest *request)
{
    String status = "{";
    status += "\"clients\":" + String(wifi->getClientCount()) + ",";
    status += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    status += "\"psram\":" + String(ESP.getFreePsram()) + ",";
    status += "\"streaming\":" + String(udpStream->isStreaming() ? "true" : "false");
    status += "}";
    request->send(200, "application/json", status);
}
