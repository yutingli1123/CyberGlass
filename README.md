# CyberGlass Camera System

ESP32S3-based smart camera system with BLE (Bluetooth Low Energy) connectivity for wireless image transfer.

## Features

### Core Capabilities
- **BLE Image Transfer**: Wireless image transfer via Bluetooth Low Energy
- **8-Channel Parallel Transfer**: Fast BLE data transmission using 8 parallel channels
- **No WiFi Required**: Pure BLE operation, no network infrastructure needed
- **Mobile-First**: Optimized for mobile app integration

### Camera Control
- **Multiple Resolutions**: QQVGA (160x120) to UXGA (1600x1200)
- **Quality Control**: Adjustable JPEG compression
- **Dual Camera Support**: OV2640 (built-in) and OV5640 (external)
- **Remote Configuration**: Change settings via BLE

## Hardware

- **Board**: Seeed XIAO ESP32S3 Sense
- **MCU**: ESP32-S3 (dual-core Xtensa, 240MHz)
- **Camera**: OV2640 (built-in) or OV5640 (external)
- **Memory**: 8MB PSRAM, 8MB Flash
- **Connectivity**: BLE 5.0, USB-C

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

### 2. Connect via BLE

The device automatically starts BLE service on boot:

- **Device Name**: `CyberGlass-XXXX`
- **Range**: ~10 meters (typical BLE range)
- **Services**: Image Control, Image Data (8 parallel channels)

Connection steps:

1. Power on the device
2. Use BLE scanner app or Python script to find `CyberGlass-XXXX`
3. Connect and start requesting images

### 3. Use Python Tools

Install Python dependencies and use BLE tools:

```bash
# Install Python dependencies
pip install -r tools/requirements.txt

# Connect and receive images via BLE
cd tools
python ble_receive_image.py
```

## Project Structure

```
CyberGlass/
├── src/
│   ├── main.cpp                  # Main entry point (BLE-only)
│   ├── camera_module.cpp         # Camera control
│   └── ble_image_transfer.cpp    # BLE image transfer service
├── include/
│   ├── camera_module.h
│   ├── ble_image_transfer.h
│   └── base64_encoder.h
├── tools/
│   ├── ble_receive_image.py      # BLE image receiver
│   ├── ble_connect_and_open.py   # BLE connection utility
│   ├── QUICKSTART.md             # Quick start guide
│   └── README.md                 # Tools documentation
├── platformio.ini                # Build configuration
├── README.md                     # This file
└── BLE_IMAGE_TRANSFER.md        # BLE protocol documentation
```

## Usage

### BLE Image Transfer

**Connection**: Use BLE scanner or Python script to connect to `CyberGlass-XXXX` (where XXXX is the device's unique MAC suffix)

**Image Request Flow**:
1. Connect to BLE device
2. Write to Image Request characteristic with resolution and quality parameters
3. Read Image Info characteristic to get image size and chunk count
4. Subscribe to Image Data characteristics (8 parallel channels)
5. Receive image data across multiple channels
6. Reassemble image from chunks

**Python Example**:
```bash
cd tools
python ble_receive_image.py
```

See [BLE_IMAGE_TRANSFER.md](BLE_IMAGE_TRANSFER.md) for detailed protocol documentation.

## BLE Services and Characteristics

### Image Control Service
- **Image Request** (Write): Request image with resolution/quality
- **Image Info** (Read/Notify): Image metadata (size, chunks, status)
- **Image Control** (Write): Control transfer (request chunk, cancel)

### Image Data Services (8 Parallel Channels)
- **Image Data 1-8** (Read/Notify): Image data chunks distributed across channels

For complete protocol specification, see [BLE_IMAGE_TRANSFER.md](BLE_IMAGE_TRANSFER.md).

## Configuration

### Camera Settings

Edit [src/camera_module.cpp](src/camera_module.cpp):

```cpp
// Default resolution
config.frame_size = FRAMESIZE_VGA;  // 640x480

// Default JPEG quality
config.jpeg_quality = 10;  // 0-63 (lower = better quality)
```

### BLE Settings

Edit [include/ble_image_transfer.h](include/ble_image_transfer.h):

```cpp
#define BLE_DEVICE_NAME "CyberGlass"     // BLE device name
#define BLE_IMAGE_CHUNK_SIZE 480         // Bytes per chunk
#define BLE_IMAGE_DATA_CHANNELS 8        // Parallel channels
```

## Performance

| Feature | Specification |
|---------|---------------|
| Connection Type | BLE 5.0 |
| Range | ~10 meters |
| Data Channels | 8 parallel |
| Chunk Size | 480 bytes |
| Max Image Size | 64 KB |
| Resolutions | QQVGA to UXGA |

**Factors affecting performance**:
- **Resolution**: Lower = faster transfer
- **Quality**: Higher value = smaller files
- **BLE distance**: Closer = better throughput
- **Parallel channels**: 8 channels for faster transfer

## Troubleshooting

### BLE Issues

- **Can't find device**:
  - Check device is powered on
  - Look for "CyberGlass-XXXX" in BLE scanner (XXXX = MAC suffix)
  - Ensure device is within ~10 meter range
  - Check serial monitor for "BLE advertising started"

- **Connection fails**:
  - Restart device and try again
  - Ensure no other device is connected
  - Check BLE is enabled on client device

- **Image transfer incomplete**:
  - Move closer to device
  - Check for BLE interference
  - Verify image size is under 64KB

### Camera Issues

- **No image/Camera failed**:
  - Check serial output for initialization errors
  - Power cycle the device
  - Verify camera cable connection

- **Corrupted images**:
  - Reduce resolution
  - Move closer to device
  - Check chunk reassembly logic

### Build Issues

- **Upload fails**: Check USB connection and correct port selected
- **Out of memory**: Use lower resolutions or reduce quality

## Development

### Dependencies

**Firmware:**
- ESP32 Arduino Framework
- ESP32 BLE Library

**Tools:**
- Python 3.7+
- bleak (BLE library for Python)
- Pillow (Image processing)

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

For detailed BLE protocol, see [BLE_IMAGE_TRANSFER.md](BLE_IMAGE_TRANSFER.md)

For migration notes, see [WIFI_HTTP_REMOVAL_SUMMARY.md](WIFI_HTTP_REMOVAL_SUMMARY.md)

For issues:
1. Check serial monitor for error messages
2. Review documentation
3. Create GitHub issue with logs
