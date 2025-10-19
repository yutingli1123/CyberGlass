# WiFi Access Point Setup Guide

## Overview

CyberGlass supports WiFi connectivity! The ESP32S3 operates as a WiFi Access Point (AP), allowing you to connect wirelessly and access the camera through any web browser - no cable needed!

## Features

- **WiFi Access Point Mode**: Device creates its own WiFi network
- **Web Interface**: Modern browser-based control panel
- **Live MJPEG Streaming**: Real-time video streaming (up to 10 FPS)
- **Single Photo Capture**: Take snapshots on demand
- **Remote Control**: Change resolution, quality, and view system status
- **Dual Mode**: Serial communication remains available for debugging
- **Multi-Client Support**: Up to 4 simultaneous connections

## Quick Start

### 1. Upload the Firmware

```bash
# Build and upload to your XIAO ESP32S3
pio run --target upload

# Monitor serial output
pio device monitor
```

### 2. Connect to WiFi

Once the device boots, you'll see output like:

```
========================================
  XIAO ESP32S3 - CyberGlass System
========================================
WiFi AP started successfully!
SSID: CyberGlass-AP
Password: cyberglass123
IP Address: 192.168.4.1
```

**Connection Steps:**
1. On your phone/laptop, open WiFi settings
2. Connect to network: `CyberGlass-AP`
3. Enter password: `cyberglass123`
4. Open browser and navigate to: `http://192.168.4.1`

### 3. Use the Web Interface

The web interface provides:

- **Capture Photo**: Take single snapshots (displayed immediately)
- **Start Stream**: Begin MJPEG video streaming (~10 FPS)
- **Stop Stream**: Stop video streaming and free resources
- **Resolution Control**: QQVGA (160x120) to UXGA (1600x1200)
- **Quality Control**: JPEG compression (5-30, lower = better quality)
- **System Status**: Real-time heap, PSRAM, and client count monitoring

## WiFi Configuration

### Default Settings

Located in [include/wifi_provisioning.h](include/wifi_provisioning.h):

```cpp
#define AP_SSID "CyberGlass-AP"
#define AP_PASSWORD "cyberglass123"
#define AP_CHANNEL 1
#define AP_MAX_CONNECTIONS 4
```

### Customization

To change WiFi settings, edit `include/wifi_provisioning.h`:

```cpp
// Change SSID
#define AP_SSID "YourCustomName"

// Change password (minimum 8 characters)
#define AP_PASSWORD "your_secure_password"

// Change WiFi channel (1-13)
#define AP_CHANNEL 6

// Change max simultaneous connections
#define AP_MAX_CONNECTIONS 2
```

After editing, rebuild and upload:

```bash
pio run --target upload
```

## Web API Endpoints

The web server exposes these HTTP endpoints:

### Main Interface
- `GET /` - Web interface (HTML)

### Camera Control
- `GET /capture` - Capture single photo (returns JPEG)
- `GET /stream` - MJPEG video stream
- `GET /stop` - Stop streaming

### Settings
- `GET /resolution?value=VGA` - Change resolution
  - Values: `QQVGA`, `QVGA`, `VGA`, `SVGA`, `XGA`, `HD`, `SXGA`, `UXGA`
- `GET /quality?value=10` - Change JPEG quality (0-63)
  - Lower values = higher quality (less compression)

### Status
- `GET /status` - Get system status (JSON)

Example status response:
```json
{
  "clients": 1,
  "heap": 182456,
  "psram": 4194304
}
```

## Using with Command Line

### Capture Photo
```bash
curl http://192.168.4.1/capture > photo.jpg
```

### Get Status
```bash
curl http://192.168.4.1/status
```

### Change Resolution
```bash
curl "http://192.168.4.1/resolution?value=HD"
```

### Stream Video (with ffplay)
```bash
ffplay http://192.168.4.1/stream
```

## Serial Commands (Still Available)

Serial interface remains functional for debugging:

```
Available commands:
- capture/c  : Capture and display photo info
- send/d     : Capture and send via serial (binary)
- stream     : Start binary video stream
- stop       : Stop streaming
- resolution : Change camera resolution
- quality    : Change JPEG quality
- status/s   : Show camera status
- help/h     : Show help
```

**Note**: Serial streaming uses binary protocol, while WiFi uses MJPEG.

## Performance

### WiFi vs Serial Comparison

| Feature | WiFi (MJPEG) | Serial (Binary) |
|---------|--------------|-----------------|
| Frame Rate | ~10 FPS | ~30+ FPS |
| Resolution | Up to UXGA | Up to UXGA |
| Range | ~50m (indoor) | Cable length |
| Clients | Up to 4 | 1 |
| Latency | ~200ms | ~50ms |

**Notes:**
- WiFi frame rate depends on resolution and network conditions
- Lower resolutions (VGA and below) achieve better frame rates
- Multiple connected clients share bandwidth

## Troubleshooting

### Cannot Connect to WiFi

1. **Check SSID**: Ensure you're connecting to `CyberGlass-AP`
2. **Verify Password**: Default is `cyberglass123`
3. **Check Serial Output**: Monitor for error messages
4. **Restart Device**: Power cycle the ESP32S3

### Web Interface Not Loading

1. **Verify IP**: Should be `192.168.4.1` (default AP IP)
2. **Check Connection**: Ensure connected to CyberGlass-AP WiFi
3. **Try Different Browser**: Chrome, Firefox, Safari all supported
4. **Clear Cache**: Refresh with Ctrl+F5 (Cmd+Shift+R on Mac)

### Camera Not Streaming / Black Screen

1. **Test Single Capture First**: Click "Capture Photo" to verify camera works
2. **Check Serial Output**: Look for "Camera frame failed" or similar errors
3. **Reduce Resolution**: Try QVGA (320x240) or VGA (640x480)
4. **Check Memory**: Open `/status` - ensure PSRAM available (>2MB)
5. **Restart Stream**: Click Stop, wait 2 seconds, then Start Stream
6. **Refresh Browser**: Clear image cache with Ctrl+F5

### Corrupted/Partial Images

1. **Memory Issue**: Reduce resolution or increase quality value
2. **WiFi Interference**: Move closer to device
3. **Multiple Clients**: Disconnect other browsers/devices

### Slow Performance

1. **Lower Resolution**: Use QVGA or VGA instead of HD/UXGA
2. **Increase Quality Value**: Higher values = more compression = smaller files
3. **Reduce Clients**: Disconnect other devices (max 4 supported)
4. **Check Distance**: Move closer to ESP32S3 (<10m recommended)

## Technical Details

### Architecture

```
┌─────────────────┐
│   ESP32S3       │
│  ┌───────────┐  │
│  │  Camera   │  │
│  └─────┬─────┘  │
│        │        │
│  ┌─────▼─────┐  │
│  │  Buffer   │  │
│  └─────┬─────┘  │
│        │        │
│  ┌─────▼─────┐  │
│  │ Web Server│◄─┼─── WiFi AP (192.168.4.1)
│  └───────────┘  │      │
└─────────────────┘      │
                         │
                    ┌────▼────┐
                    │ Clients │
                    │ (Phones │
                    │ Laptops)│
                    └─────────┘
```

### Memory Usage

- **Web Server**: ~50KB heap
- **WiFi Stack**: ~40KB heap
- **Frame Buffer**: PSRAM (required for high resolutions)
- **Each JPEG Frame**: 10KB (QVGA) to 200KB (UXGA)
- **Recommended**: 4MB+ PSRAM, 200KB+ free heap

### Implementation Details

**Streaming Method**: MJPEG (Motion JPEG)
- Each frame is a complete JPEG image
- Sent with `multipart/x-mixed-replace` content type
- Browser automatically displays frames as video

**Buffer Management**:
- Frame buffers captured via `esp_camera_fb_get()`
- Buffers released after transmission completes
- Prevents corruption by managing buffer lifetime in callbacks

**Frame Rate Control**:
- 100ms delay between frames = ~10 FPS
- Adjustable via `frameDelay` constant in `web_server.cpp`

### Libraries Used

- **ESPAsyncWebServer**: Asynchronous HTTP server (GitHub: me-no-dev)
- **AsyncTCP**: TCP library for ESP32 (GitHub: me-no-dev)
- **WiFi**: ESP32 WiFi driver (built-in ESP-IDF)
- **esp_camera**: Camera driver (built-in ESP-IDF)

## Security Considerations

**Important**: This is a basic setup suitable for development/testing.

### Current Security
- ✅ WPA2 password protection
- ✅ Limited to 4 simultaneous connections
- ✅ No internet exposure (AP mode only)

### Not Included (Production Use)
- ❌ HTTPS/TLS encryption
- ❌ User authentication
- ❌ Access control lists
- ❌ Firmware update over WiFi

### Recommendations for Production
1. Change default password in code
2. Implement HTTPS with certificates
3. Add authentication (username/password)
4. Use WPA3 when available
5. Consider VPN for remote access

## Next Steps

### Possible Enhancements
- [ ] Connect to existing WiFi (Station mode)
- [ ] mDNS support (access via `cyberglass.local`)
- [ ] WebSocket for lower latency
- [ ] Motion detection alerts
- [ ] Image storage to SD card
- [ ] Time-lapse functionality
- [ ] Mobile app support

## Support

For issues or questions:
1. Check serial monitor output for errors
2. Review this documentation
3. Check GitHub issues
4. Create new issue with logs

## License

Same as main project.
