#include "web_server.h"

// Frame delay for streaming (milliseconds)
const unsigned long frameDelay = 100; // 10 FPS

// Global state for streaming
static camera_fb_t *streamFrameBuffer = nullptr;
static size_t streamFrameIndex = 0;
static bool streamHeaderSent = false;
static volatile bool streamActive = false;

// Stream handler function
static void streamJpg(AsyncWebServerRequest *request){
    Serial.println("Stream request received");
    streamActive = true;

    AsyncWebServerResponse *response = request->beginChunkedResponse(
        "multipart/x-mixed-replace;boundary=frame",
        [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            // Check if stream should stop
            if (!streamActive) {
                Serial.println("Stream stopped by user");
                if (streamFrameBuffer) {
                    esp_camera_fb_return(streamFrameBuffer);
                    streamFrameBuffer = nullptr;
                }
                return 0;
            }

            // Get new frame if needed
            if (streamFrameBuffer == nullptr) {
                streamFrameBuffer = esp_camera_fb_get();
                if (!streamFrameBuffer) {
                    Serial.println("ERROR: Camera frame failed!");
                    return 0;
                }
                Serial.printf("New frame: %d bytes\n", streamFrameBuffer->len);
                streamFrameIndex = 0;
                streamHeaderSent = false;
            }

            size_t bytesWritten = 0;

            // Send header first
            if (!streamHeaderSent) {
                String header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ";
                header += String(streamFrameBuffer->len);
                header += "\r\n\r\n";

                size_t headerLen = header.length();
                if (headerLen > maxLen) {
                    esp_camera_fb_return(streamFrameBuffer);
                    streamFrameBuffer = nullptr;
                    return 0;
                }

                memcpy(buffer, header.c_str(), headerLen);
                bytesWritten = headerLen;
                streamHeaderSent = true;
                return bytesWritten;
            }

            // Send frame data
            size_t remaining = streamFrameBuffer->len - streamFrameIndex;
            size_t toSend = min(remaining, maxLen - 2); // Reserve 2 bytes for \r\n

            if (toSend > 0) {
                memcpy(buffer, streamFrameBuffer->buf + streamFrameIndex, toSend);
                streamFrameIndex += toSend;
                bytesWritten = toSend;
            }

            // Check if frame is complete
            if (streamFrameIndex >= streamFrameBuffer->len) {
                // Add trailing \r\n
                if (bytesWritten + 2 <= maxLen) {
                    buffer[bytesWritten++] = '\r';
                    buffer[bytesWritten++] = '\n';
                }

                // Release frame and prepare for next
                esp_camera_fb_return(streamFrameBuffer);
                streamFrameBuffer = nullptr;
                streamFrameIndex = 0;
                streamHeaderSent = false;

                // Small delay for frame rate control
                delay(frameDelay);
            }

            return bytesWritten;
        }
    );

    request->send(response);
}

WebServerManager::WebServerManager(AsyncWebServer* serverPtr, WiFiProvisioning* wifiPtr)
    : server(serverPtr), wifi(wifiPtr) {
}

void WebServerManager::setupRoutes() {
    // Root page - Web interface
    server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request){
        this->handleRoot(request);
    });

    // Capture single photo
    server->on("/capture", HTTP_GET, [this](AsyncWebServerRequest *request){
        this->handleCapture(request);
    });

    // MJPEG Stream endpoint
    server->on("/stream", HTTP_GET, [this](AsyncWebServerRequest *request){
        this->handleStream(request);
    });

    // Stop streaming
    server->on("/stop", HTTP_GET, [this](AsyncWebServerRequest *request){
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

    // Status endpoint
    server->on("/status", HTTP_GET, [this](AsyncWebServerRequest *request){
        this->handleStatus(request);
    });

    Serial.println("Web server routes configured");
}

void WebServerManager::handleRoot(AsyncWebServerRequest *request) {
    String html = generateHTML();
    request->send(200, "text/html", html);
}

void WebServerManager::handleCapture(AsyncWebServerRequest *request) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Capture: Camera failed");
        request->send(500, "text/plain", "Camera capture failed");
        return;
    }

    Serial.printf("Capture: Got frame %d bytes\n", fb->len);

    // Send image with callback that manages buffer lifetime
    AsyncWebServerResponse *response = request->beginResponse(
        "image/jpeg",
        fb->len,
        [fb](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            size_t remaining = fb->len - index;
            size_t toSend = min(remaining, maxLen);

            memcpy(buffer, fb->buf + index, toSend);

            // Release buffer when done
            if (index + toSend >= fb->len) {
                esp_camera_fb_return(fb);
            }

            return toSend;
        }
    );

    request->send(response);
}

void WebServerManager::handleStream(AsyncWebServerRequest *request) {
    streamJpg(request);
}

void WebServerManager::handleStop(AsyncWebServerRequest *request) {
    streamActive = false;
    Serial.println("Stop request received");
    request->send(200, "text/plain", "Stream stop signal sent");
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

void WebServerManager::handleStatus(AsyncWebServerRequest *request) {
    String status = "{";
    status += "\"clients\":" + String(wifi->getClientCount()) + ",";
    status += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    status += "\"psram\":" + String(ESP.getFreePsram());
    status += "}";
    request->send(200, "application/json", status);
}

String WebServerManager::generateHTML() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>CyberGlass Camera</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f0f0f0; }";
    html += "h1 { color: #333; }";
    html += ".container { max-width: 900px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
    html += ".video-container { text-align: center; margin: 20px 0; background: #000; border-radius: 4px; min-height: 400px; display: flex; align-items: center; justify-content: center; }";
    html += "#stream { max-width: 100%; height: auto; }";
    html += ".controls { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 10px; margin: 20px 0; }";
    html += "button { padding: 12px 20px; font-size: 16px; cursor: pointer; background: #007bff; color: white; border: none; border-radius: 4px; }";
    html += "button:hover { background: #0056b3; }";
    html += "button.stop { background: #dc3545; }";
    html += "button.stop:hover { background: #c82333; }";
    html += ".info { background: #e9ecef; padding: 15px; border-radius: 4px; margin: 10px 0; }";
    html += "select { padding: 10px; font-size: 14px; border-radius: 4px; border: 1px solid #ccc; width: 100%; }";
    html += "label { display: block; margin-bottom: 5px; font-weight: bold; color: #555; }";
    html += ".message { padding: 10px; margin: 10px 0; border-radius: 4px; display: none; }";
    html += ".message.success { background: #d4edda; color: #155724; display: block; }";
    html += ".message.error { background: #f8d7da; color: #721c24; display: block; }";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>CyberGlass Camera System</h1>";
    html += "<div class='info'>";
    html += "<strong>WiFi AP:</strong> " + wifi->getSSID() + " | ";
    html += "<strong>IP:</strong> " + wifi->getAPIP().toString();
    html += "</div>";
    html += "<div id='message' class='message'></div>";
    html += "<div class='video-container'>";
    html += "<img id='stream' src='' alt='Camera stream will appear here' />";
    html += "</div>";
    html += "<div class='controls'>";
    html += "<button onclick='capturePhoto()'>Capture Photo</button>";
    html += "<button onclick='startStream()'>Start Stream</button>";
    html += "<button class='stop' onclick='stopStream()'>Stop Stream</button>";
    html += "</div>";
    html += "<div class='controls'>";
    html += "<div><label>Resolution:</label><select id='resolution' onchange='changeRes(this.value)'>";
    html += "<option value='QQVGA'>QQVGA (160x120)</option>";
    html += "<option value='QVGA'>QVGA (320x240)</option>";
    html += "<option value='VGA' selected>VGA (640x480)</option>";
    html += "<option value='SVGA'>SVGA (800x600)</option>";
    html += "<option value='XGA'>XGA (1024x768)</option>";
    html += "<option value='HD'>HD (1280x720)</option>";
    html += "<option value='SXGA'>SXGA (1280x1024)</option>";
    html += "<option value='UXGA'>UXGA (1600x1200)</option>";
    html += "</select></div>";
    html += "<div><label>Quality (0-63):</label><select id='quality' onchange='changeQual(this.value)'>";
    html += "<option value='5'>5 (High Quality)</option>";
    html += "<option value='10' selected>10 (Default)</option>";
    html += "<option value='15'>15</option>";
    html += "<option value='20'>20</option>";
    html += "<option value='25'>25</option>";
    html += "<option value='30'>30 (Lower Quality)</option>";
    html += "</select></div>";
    html += "</div>";
    html += "<script>";
    html += "function showMessage(msg, type) {";
    html += "  const el = document.getElementById('message');";
    html += "  el.textContent = msg;";
    html += "  el.className = 'message ' + type;";
    html += "  setTimeout(() => { el.style.display = 'none'; }, 3000);";
    html += "}";
    html += "function capturePhoto() {";
    html += "  document.getElementById('stream').src = '/capture?' + Date.now();";
    html += "  showMessage('Photo captured!', 'success');";
    html += "}";
    html += "function startStream() {";
    html += "  document.getElementById('stream').src = '/stream?' + Date.now();";
    html += "  showMessage('Stream started', 'success');";
    html += "}";
    html += "function stopStream() {";
    html += "  document.getElementById('stream').src = '';";
    html += "  fetch('/stop').then(() => showMessage('Stream stopped', 'success'));";
    html += "}";
    html += "function changeRes(val) {";
    html += "  fetch('/resolution?value=' + val)";
    html += "    .then(r => r.text())";
    html += "    .then(msg => showMessage(msg, 'success'))";
    html += "    .catch(err => showMessage('Error: ' + err, 'error'));";
    html += "}";
    html += "function changeQual(val) {";
    html += "  fetch('/quality?value=' + val)";
    html += "    .then(r => r.text())";
    html += "    .then(msg => showMessage(msg, 'success'))";
    html += "    .catch(err => showMessage('Error: ' + err, 'error'));";
    html += "}";
    html += "</script>";
    html += "</div></body></html>";

    return html;
}
