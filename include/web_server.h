#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "camera_module.h"
#include "udp_stream.h"
#include "wifi_provisioning.h"

// Web Server Manager Class - API Endpoints Only
class WebServerManager {
public:
  WebServerManager(AsyncWebServer *serverPtr, WiFiProvisioning *wifiPtr, UDPStream *udpPtr);

  // Setup all web server routes and handlers
  void setupRoutes();

private:
  AsyncWebServer *server;
  WiFiProvisioning *wifi;
  UDPStream *udpStream;

  // Rate limiting for capture endpoint
  unsigned long lastCaptureTime;
  static const unsigned long MIN_CAPTURE_INTERVAL = 100; // 100ms = 10 FPS max
  static const size_t MIN_FREE_PSRAM = 200000; // 200KB minimum free PSRAM

  // API endpoint handlers
  void handleCapture(AsyncWebServerRequest *request);
  void handleStart(AsyncWebServerRequest *request);
  void handleStop(AsyncWebServerRequest *request);
  void handleResolution(AsyncWebServerRequest *request);
  void handleQuality(AsyncWebServerRequest *request);
  void handleStatus(AsyncWebServerRequest *request);
};

#endif // WEB_SERVER_H
