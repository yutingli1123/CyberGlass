#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "camera_module.h"
#include "wifi_provisioning.h"

// Web Server Manager Class
class WebServerManager {
public:
    WebServerManager(AsyncWebServer* serverPtr, WiFiProvisioning* wifiPtr);

    // Setup all web server routes and handlers
    void setupRoutes();

private:
    AsyncWebServer* server;
    WiFiProvisioning* wifi;

    // Route handlers
    void handleRoot(AsyncWebServerRequest *request);
    void handleCapture(AsyncWebServerRequest *request);
    void handleStream(AsyncWebServerRequest *request);
    void handleStop(AsyncWebServerRequest *request);
    void handleResolution(AsyncWebServerRequest *request);
    void handleQuality(AsyncWebServerRequest *request);
    void handleStatus(AsyncWebServerRequest *request);

    // Helper functions
    String generateHTML();
};

#endif // WEB_SERVER_H
