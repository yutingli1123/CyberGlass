#include "web_server.h"

WebServerManager::WebServerManager(AsyncWebServer* serverPtr, WiFiProvisioning* wifiPtr, UDPStream* udpPtr)
    : server(serverPtr), wifi(wifiPtr), udpStream(udpPtr), lastCaptureTime(0) {
    Serial.println("Web server manager initialized");
}

void WebServerManager::setupRoutes() {
    // Capture single photo (JPEG)
    server->on("/capture", HTTP_GET, [this](AsyncWebServerRequest *request){
        this->handleCapture(request);
    });

    // Start UDP video stream
    server->on("/stream/start", HTTP_GET, [this](AsyncWebServerRequest *request){
        this->handleStart(request);
    });

    // Stop UDP video stream
    server->on("/stream/stop", HTTP_GET, [this](AsyncWebServerRequest *request){
        this->handleStop(request);
    });

    // Change resolution
    server->on("/resolution", HTTP_GET, [this](AsyncWebServerRequest *request){
        this->handleResolution(request);
    });

    // Change quality
    server->on("/quality", HTTP_GET, [this](AsyncWebServerRequest *request){
        this->handleQuality(request);
    });

    // Status endpoint (JSON)
    server->on("/status", HTTP_GET, [this](AsyncWebServerRequest *request){
        this->handleStatus(request);
    });

    Serial.println("API endpoints configured:");
    Serial.println("  GET /capture - Capture single photo");
    Serial.println("  GET /stream/start - Start UDP video stream");
    Serial.println("  GET /stream/stop - Stop UDP video stream");
    Serial.println("  GET /resolution?value=<res> - Change resolution");
    Serial.println("  GET /quality?value=<num> - Change quality");
    Serial.println("  GET /status - Get device status (JSON)");
}

void WebServerManager::handleCapture(AsyncWebServerRequest *request) {
    // Rate limiting: prevent concurrent requests that could exhaust frame buffers
    unsigned long now = millis();
    if (now - lastCaptureTime < MIN_CAPTURE_INTERVAL) {
        request->send(429, "text/plain", "Too many requests");
        return;
    }

    // Check PSRAM availability before burst capture
    size_t freePsram = ESP.getFreePsram();
    if (freePsram < MIN_FREE_PSRAM * 2) {
        Serial.printf("Capture: Insufficient PSRAM - %d bytes free\n", freePsram);
        request->send(503, "text/plain", "Low memory");
        return;
    }

    lastCaptureTime = now;

    // Burst capture: take 5 shots, select largest (sharpest image, less JPEG compression)
    camera_fb_t* bestFrame = nullptr;
    size_t bestSize = 0;
    const int NUM_SHOTS = 5;

    Serial.println("Capture: Starting 5-shot burst...");

    for (int i = 0; i < NUM_SHOTS; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            Serial.printf("Capture: Shot %d/%d failed\n", i + 1, NUM_SHOTS);
            continue;
        }

        Serial.printf("Capture: Shot %d/%d = %d bytes\n", i + 1, NUM_SHOTS, fb->len);

        // Larger file size = less compression = sharper image
        if (fb->len > bestSize) {
            if (bestFrame) {
                esp_camera_fb_return(bestFrame);
            }
            bestFrame = fb;
            bestSize = fb->len;
        } else {
            esp_camera_fb_return(fb);
        }

        // Delay between shots for auto-exposure/white-balance adjustment
        if (i < NUM_SHOTS - 1) {
            delay(20);
        }
    }

    if (!bestFrame) {
        Serial.println("Capture: All 5 shots failed");
        request->send(500, "text/plain", "Camera failed");
        return;
    }

    Serial.printf("Capture: Selected best frame = %d bytes (PSRAM free: %d KB)\n",
                  bestFrame->len, freePsram / 1024);

    // Copy to heap, then immediately release camera buffer to prevent deadlock
    uint8_t* buffer = (uint8_t*)malloc(bestFrame->len);
    if (!buffer) {
        Serial.println("Capture: Heap allocation failed");
        esp_camera_fb_return(bestFrame);
        request->send(500, "text/plain", "Out of memory");
        return;
    }

    memcpy(buffer, bestFrame->buf, bestFrame->len);
    size_t len = bestFrame->len;
    esp_camera_fb_return(bestFrame);

    // Send JPEG response (heap buffer freed automatically when transfer completes)
    AsyncWebServerResponse *response = request->beginResponse(
        "image/jpeg",
        len,
        [buffer, len](uint8_t *outputBuffer, size_t maxLen, size_t index) -> size_t {
            size_t remaining = len - index;
            if (remaining == 0) {
                free(buffer);
                return 0;
            }
            size_t toSend = min(remaining, maxLen);
            memcpy(outputBuffer, buffer + index, toSend);
            return toSend;
        }
    );

    request->send(response);
}

void WebServerManager::handleResolution(AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
        String res = request->getParam("value")->value();
        framesize_t size = FRAMESIZE_VGA;

        if (res == "QQVGA") size = FRAMESIZE_QQVGA;
        else if (res == "QVGA") size = FRAMESIZE_QVGA;
        else if (res == "VGA") size = FRAMESIZE_VGA;
        else if (res == "SVGA") size = FRAMESIZE_SVGA;
        else if (res == "XGA") size = FRAMESIZE_XGA;
        else if (res == "HD") size = FRAMESIZE_HD;
        else if (res == "SXGA") size = FRAMESIZE_SXGA;
        else if (res == "UXGA") size = FRAMESIZE_UXGA;

        changeResolution(size);
        request->send(200, "text/plain", "Resolution changed to " + res);
    } else {
        request->send(400, "text/plain", "Missing value parameter");
    }
}

void WebServerManager::handleQuality(AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
        int quality = request->getParam("value")->value().toInt();
        changeQuality(quality);
        request->send(200, "text/plain", "Quality changed to " + String(quality));
    } else {
        request->send(400, "text/plain", "Missing value parameter");
    }
}

void WebServerManager::handleStart(AsyncWebServerRequest *request) {
    IPAddress clientIP = request->client()->remoteIP();
    udpStream->start(clientIP, UDP_PORT);

    Serial.printf("Start: Streaming to %s:%d\n", clientIP.toString().c_str(), UDP_PORT);

    String response = "{";
    response += "\"status\":\"streaming\",";
    response += "\"client\":\"" + clientIP.toString() + "\",";
    response += "\"port\":" + String(UDP_PORT);
    response += "}";
    request->send(200, "application/json", response);
}

void WebServerManager::handleStop(AsyncWebServerRequest *request) {
    udpStream->stop();
    Serial.println("Stop: Streaming stopped");
    request->send(200, "application/json", "{\"status\":\"stopped\"}");
}

void WebServerManager::handleStatus(AsyncWebServerRequest *request) {
    String status = "{";
    status += "\"clients\":" + String(wifi->getClientCount()) + ",";
    status += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    status += "\"psram\":" + String(ESP.getFreePsram()) + ",";
    status += "\"streaming\":" + String(udpStream->isStreaming() ? "true" : "false");
    status += "}";
    request->send(200, "application/json", status);
}
