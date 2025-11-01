#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "camera_module.h"
#include "network_provisioning.h"

// Web Server Manager Class - API Endpoints Only
class WebServerManager {
public:
  WebServerManager(AsyncWebServer *serverPtr, WiFiProvisioning *wifiPtr);

  // Setup all web server routes and handlers
  void setupRoutes();

private:
  AsyncWebServer *server;
  WiFiProvisioning *wifi;

  // Rate limiting for capture endpoint
  unsigned long lastCaptureTime;
  static constexpr unsigned long MIN_CAPTURE_INTERVAL = 100; // 100ms = 10 FPS max
  static constexpr size_t MIN_FREE_PSRAM = 200000; // 200KB minimum free PSRAM

  // API endpoint handlers
  void handleCapture(AsyncWebServerRequest *request);
  static void handleResolution(AsyncWebServerRequest *request);
  static void handleQuality(AsyncWebServerRequest *request);
  void handleStatus(AsyncWebServerRequest *request) const;
};

#endif // WEB_SERVER_H
