# CyberGlass Camera System

ESP32S3-based smart camera system with WiFi and serial connectivity for wireless video streaming and remote control.

## Features

### Core Capabilities
- **WiFi Access Point**: Creates standalone WiFi network (no router needed)
- **Web Interface**: Browser-based control panel with live preview
- **MJPEG Streaming**: Real-time video streaming over WiFi (~10 FPS)
- **Serial Interface**: USB fallback for debugging and binary streaming (~30 FPS)
- **Multi-Client**: Supports up to 4 simultaneous WiFi connections

### Camera Control
- **Multiple Resolutions**: QQVGA (160x120) to UXGA (1600x1200)
- **Quality Control**: Adjustable JPEG compression (5-30)
- **Dual Camera Support**: OV2640 (built-in) and OV5640 (external)
- **Remote Configuration**: Change settings via web or serial

## Hardware

- **Board**: Seeed XIAO ESP32S3 Sense
- **MCU**: ESP32-S3 (dual-core Xtensa, 240MHz)
- **Camera**: OV2640 (built-in) or OV5640 (external)
- **Memory**: 8MB PSRAM, 8MB Flash
- **Connectivity**: WiFi 802.11 b/g/n, USB-C

## Quick Start

### 1. Build and Upload Firmware

```bash
# Install PlatformIO (if not already installed)
pip install platformio

# Build and upload firmware
pio run --target upload

# Monitor serial output (optional)
pio device monitor
```

### 2. Connect to WiFi

The device creates its own WiFi network on boot:

- **SSID**: `CyberGlass-AP`
- **Password**: `cyberglass123`
- **IP Address**: `192.168.4.1`

Connection steps:
1. Connect your phone/laptop to `CyberGlass-AP` WiFi
2. Open browser to `http://192.168.4.1`
3. Use web interface to control camera

### 3. Use Web Interface

- Click **Capture Photo** to take a snapshot
- Click **Start Stream** to begin live video
- Click **Stop Stream** to end streaming
- Adjust **Resolution** and **Quality** as needed

### 4. Optional: Serial Tools (Advanced)

For development and debugging, install Python tools:

```bash
# Install Python dependencies
pip install -r requirements.txt

# Use serial streaming tools
./fast_video_stream.sh   # Binary streaming (fastest)
./video_stream.sh        # Alternative viewer
./receive.sh             # Single photo capture
```

## Project Structure

```
CyberGlass/
├── src/
│   ├── main.cpp              # Main entry point
│   ├── camera_module.cpp     # Camera control
│   ├── wifi_provisioning.cpp # WiFi AP management
│   ├── web_server.cpp        # HTTP server & routes
│   └── base64_encoder.cpp    # Base64 encoding
├── include/
│   ├── camera_module.h
│   ├── wifi_provisioning.h
│   ├── web_server.h
│   └── base64_encoder.h
├── tools/
│   ├── receive_photo.py      # Serial photo receiver
│   ├── video_stream.py       # Serial video viewer (Base64)
│   └── fast_video_stream.py  # Serial video viewer (Binary)
├── platformio.ini            # Build configuration
├── requirements.txt          # Python dependencies
├── README.md                 # This file
└── WIFI_SETUP.md            # WiFi detailed guide
```

## Usage

### Web Interface (Recommended)

**Access**: Connect to `CyberGlass-AP` WiFi, then open `http://192.168.4.1`

**Controls**:
- **Capture Photo**: Take single snapshot (instant display)
- **Start Stream**: Begin MJPEG video streaming
- **Stop Stream**: End streaming and free resources
- **Resolution**: Select from QQVGA to UXGA
- **Quality**: Adjust JPEG compression (5-30)

**Tips**:
- Use VGA or lower for smooth streaming
- Lower quality value = better image, larger file
- Multiple browsers can view simultaneously (max 4)

### Serial Interface (Development)

Connect via USB and send commands:

```
Available commands:
  capture/c    - Capture photo and show info
  send/d       - Capture and send binary data
  stream       - Start binary video stream (use with fast_video_stream.py)
  stop         - Stop streaming
  resolution/r - Change resolution
  quality/q    - Set JPEG quality (0-63)
  status/s     - Show camera and memory status
  help/h       - Show help
```

**Note**: Serial uses binary protocol, WiFi uses MJPEG.

## API Endpoints

### HTTP (WiFi)

- `GET /` - Web interface
- `GET /capture` - Capture photo (JPEG)
- `GET /stream` - MJPEG video stream
- `GET /status` - System status (JSON)
- `GET /resolution?value=VGA` - Change resolution
- `GET /quality?value=10` - Change quality

### Examples

```bash
# Capture photo
curl http://192.168.4.1/capture > photo.jpg

# Get status
curl http://192.168.4.1/status

# Stream with VLC
vlc http://192.168.4.1/stream
```

## Configuration

### WiFi Settings

Edit [include/wifi_provisioning.h](include/wifi_provisioning.h):

```cpp
#define AP_SSID "CyberGlass-AP"        // Change WiFi name
#define AP_PASSWORD "cyberglass123"    // Change password (min 8 chars)
#define AP_CHANNEL 1                   // WiFi channel (1-13)
#define AP_MAX_CONNECTIONS 4           // Max simultaneous clients
```

### Camera Settings

Edit [src/camera_module.cpp](src/camera_module.cpp):

```cpp
// Default resolution
config.frame_size = FRAMESIZE_VGA;  // 640x480

// Default JPEG quality
config.jpeg_quality = 10;  // 0-63 (lower = better quality)
```

## Performance

| Connection | Frame Rate | Latency | Range | Best Use |
|------------|------------|---------|-------|----------|
| WiFi MJPEG | ~10 FPS | ~200ms | ~50m | General use, demos |
| Serial Binary | ~30 FPS | ~50ms | Cable | Development, high FPS |

**Factors affecting performance**:
- **Resolution**: Lower = faster (QVGA recommended for WiFi)
- **Quality**: Higher value = smaller files = faster transfer
- **WiFi distance**: Closer = better throughput
- **Clients**: Each client reduces available bandwidth

## Troubleshooting

### WiFi Issues

- **Can't connect to AP**:
  - Check SSID is `CyberGlass-AP` in WiFi list
  - Verify password: `cyberglass123` (case-sensitive)
  - Check serial output for "WiFi AP started successfully"

- **Web page won't load**:
  - Ensure connected to `CyberGlass-AP` network
  - Navigate to exactly `http://192.168.4.1`
  - Try different browser (Chrome/Firefox/Safari)

- **Streaming shows black screen**:
  - Test with "Capture Photo" first to verify camera works
  - Check serial output for error messages
  - Try lower resolution (QVGA or VGA)
  - Refresh browser with Ctrl+F5

### Camera Issues

- **No image/Camera failed**:
  - Check serial output for initialization errors
  - Power cycle the device
  - Verify camera cable connection

- **Corrupted images**:
  - Reduce resolution
  - Move closer to device (WiFi interference)
  - Disconnect other clients

- **Low frame rate**:
  - Use QVGA (320x240) for WiFi
  - Increase quality value (20-30)
  - Reduce number of connected clients

### Build Issues

- **AsyncTCP errors**: Libraries will auto-download from GitHub
- **Upload fails**: Check USB connection and correct port selected
- **Out of memory**: Normal with high resolutions - use QVGA/VGA

## Development

### Dependencies

**Firmware:**
- ESP32 Arduino Framework
- ESP Async WebServer
- AsyncTCP

**Tools:**
- Python 3.7+
- pyserial
- opencv-python
- numpy

### Building from Source

```bash
# Clone repository
git clone <repository-url>
cd CyberGlass

# Install PlatformIO
pip install platformio

# Build
pio run

# Upload and monitor
pio run --target upload && pio device monitor
```

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

[Add your license here]

## Credits

- **Hardware**: Seeed XIAO ESP32S3 Sense
- **Libraries**: ESP32 Arduino, ESPAsyncWebServer
- **Camera Drivers**: Espressif ESP-IDF

## Support

For detailed WiFi setup, see [WIFI_SETUP.md](WIFI_SETUP.md)

For issues:
1. Check serial monitor for error messages
2. Review documentation
3. Create GitHub issue with logs

## Roadmap

### Completed ✅
- [x] Serial binary streaming (30 FPS)
- [x] WiFi Access Point mode
- [x] Web-based control interface
- [x] MJPEG streaming over WiFi (10 FPS)
- [x] Multi-client support (4 connections)
- [x] Dynamic resolution/quality control
- [x] Proper buffer management (no corruption)

### Planned 🚀
- [ ] WiFi Station mode (connect to existing WiFi network)
- [ ] mDNS support (access via `cyberglass.local`)
- [ ] WebSocket streaming (lower latency)
- [ ] Motion detection with notifications
- [ ] SD card image storage
- [ ] Time-lapse recording
- [ ] Mobile app (iOS/Android)
- [ ] HTTPS/TLS encryption

---

Made with ESP32S3 ❤️
