# WiFi Access Point & BLE Provisioning Guide

## Overview

CyberGlass supports WiFi connectivity with secure BLE provisioning! Each device generates a unique SSID and random password, which can be retrieved wirelessly via Bluetooth Low Energy (BLE) - no cable or serial monitor needed!

## Features

- **Unique WiFi Credentials**: Each device has its own SSID (CyberGlass-XXXX) and random password
- **BLE Provisioning**: Get WiFi credentials wirelessly via Bluetooth
- **WiFi Access Point Mode**: Device creates its own WiFi network
- **Web Interface**: Modern browser-based control panel
- **Live MJPEG Streaming**: Real-time video streaming (up to 10 FPS)
- **Persistent Storage**: Credentials saved to flash (survives reboot)
- **Multi-Client Support**: Up to 4 simultaneous WiFi connections

## Quick Start

### 1. Upload the Firmware

```bash
# Build and upload to your XIAO ESP32S3
pio run --target upload

# Monitor serial output
pio device monitor
```

### 2. Get WiFi Credentials

Each device generates **unique credentials** on first boot. There are two ways to get them:

#### Option A: Via BLE (Recommended - No Cable Needed!)

1. **Install a BLE Scanner App**:
   - iOS: **nRF Connect** (App Store)
   - Android: **nRF Connect** (Play Store)

2. **Scan for Device**:
   - Open app and tap **SCAN**
   - Find device named `CyberGlass-XXXX` (where XXXX is your device ID)

3. **Connect and Read**:
   - Tap **CONNECT**
   - Tap **Unknown Service** (UUID: 4faf...)
   - Read two characteristics:
     - First = **WiFi SSID**
     - Second = **WiFi Password**

4. **Note Down** the SSID and password

#### Option B: Via Serial Monitor (Cable Required)

```bash
pio device monitor
```

Output will show:
```
========================================
WiFi Credentials (also available via BLE):
  SSID: CyberGlass-A1B2
  Password: a3k7m2pqw9rs
  Device ID: A1B2
BLE Device Name: CyberGlass-A1B2
Web Interface: http://192.168.4.1
========================================
```

### 3. Connect to WiFi

1. On your phone/laptop, open WiFi settings
2. Connect to network: `CyberGlass-XXXX` (your device's SSID)
3. Enter the password you retrieved via BLE or serial
4. Open browser and navigate to: `http://192.168.4.1`

### 4. Use the Web Interface

The web interface provides:

- **Capture Photo**: Take single snapshots (displayed immediately)
- **Start Stream**: Begin MJPEG video streaming (~10 FPS)
- **Stop Stream**: Stop video streaming and free resources
- **Resolution Control**: QQVGA (160x120) to UXGA (1600x1200)
- **Quality Control**: JPEG compression (5-30, lower = better quality)
- **Credentials Display**: View your WiFi SSID, password, and device ID

## BLE Provisioning Details

### How It Works

1. **Device ID Generation**:
   - Derived from ESP32S3 MAC address
   - Last 4 hex digits (e.g., `A1B2`)
   - Unique to each device

2. **Credential Generation** (First Boot):
   - SSID: `CyberGlass-{DeviceID}`
   - Password: 12 random characters (`abcdefghjkmnpqrstuvwxyz23456789`)
   - Example: `a3k7m2pqw9rs`

3. **Persistent Storage**:
   - Saved to NVS (Non-Volatile Storage)
   - Survives reboots and power loss
   - Same credentials every time

4. **BLE Service**:
   - Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
   - SSID Characteristic: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
   - Password Characteristic: `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e`

### Using BLE with Different Tools

#### Web Bluetooth (Chrome/Edge Browser)

Save this as `cyberglass-ble.html`:

```html
<!DOCTYPE html>
<html>
<body>
    <h1>CyberGlass BLE Provisioning</h1>
    <button onclick="connect()">Get WiFi Credentials</button>
    <div id="result"></div>
    <script>
        async function connect() {
            const device = await navigator.bluetooth.requestDevice({
                filters: [{ namePrefix: 'CyberGlass-' }],
                optionalServices: ['4fafc201-1fb5-459e-8fcc-c5c9c331914b']
            });
            const server = await device.gatt.connect();
            const service = await server.getPrimaryService('4fafc201-1fb5-459e-8fcc-c5c9c331914b');

            const ssidChar = await service.getCharacteristic('beb5483e-36e1-4688-b7f5-ea07361b26a8');
            const ssid = new TextDecoder().decode(await ssidChar.readValue());

            const passChar = await service.getCharacteristic('1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e');
            const password = new TextDecoder().decode(await passChar.readValue());

            document.getElementById('result').innerHTML =
                `<h2>SSID: ${ssid}</h2><h2>Password: ${password}</h2>`;
        }
    </script>
</body>
</html>
```

#### Python Script

```python
import asyncio
from bleak import BleakScanner, BleakClient

async def get_credentials():
    devices = await BleakScanner.discover()
    device = [d for d in devices if d.name and d.name.startswith("CyberGlass-")][0]

    async with BleakClient(device) as client:
        ssid = (await client.read_gatt_char("beb5483e-36e1-4688-b7f5-ea07361b26a8")).decode()
        password = (await client.read_gatt_char("1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e")).decode()
        print(f"SSID: {ssid}\nPassword: {password}")

asyncio.run(get_credentials())
```

### Resetting Credentials

To generate new WiFi credentials, erase NVS storage via serial:

```
esptool.py --port /dev/ttyUSB0 erase_region 0x9000 0x6000
```

Then reboot - new credentials will be generated.

## WiFi Configuration

### Settings

Located in [include/wifi_provisioning.h](include/wifi_provisioning.h):

```cpp
#define AP_SSID_PREFIX "CyberGlass-"  // Prefix for SSID
#define AP_CHANNEL 1                   // WiFi channel (1-13)
#define AP_MAX_CONNECTIONS 4           // Max simultaneous clients
```

### Customization

You can modify:
- **SSID Prefix**: Change `AP_SSID_PREFIX` to customize name
- **Password Length**: Edit `generatePassword()` function
- **WiFi Channel**: Change `AP_CHANNEL` (avoid interference)

After editing, rebuild:

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

###Cannot Connect to WiFi / BLE

1. **BLE Device Not Found**:
   - Check serial output for "BLE advertising started"
   - Ensure Bluetooth enabled on phone
   - Move within 5 meters of device
   - Grant location permissions to BLE scanner app

2. **WiFi SSID Not Visible**:
   - Check serial output for WiFi AP status
   - Look for `CyberGlass-XXXX` (your device's unique ID)
   - Try refreshing WiFi list
   - Restart device

3. **Wrong Password**:
   - Use BLE to retrieve correct password
   - Check serial output
   - Password is case-sensitive

4. **Connection Timeout**:
   - Power cycle ESP32S3
   - Try from different device
   - Check for WiFi interference

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

**Important**: This setup is suitable for development, testing, and local use.

### Current Security Features

✅ **Unique Credentials**: Each device has random SSID and password
✅ **WPA2 Encryption**: WiFi uses WPA2-PSK
✅ **Random Password**: 12-character random password
✅ **Persistent Storage**: NVS storage (encrypted by ESP32)
✅ **Limited Connections**: Max 4 simultaneous clients
✅ **No Internet**: AP mode (not connected to internet)

### Security Limitations

⚠️ **BLE Open Read**: Anyone in BLE range can read WiFi credentials
⚠️ **No BLE Pairing**: No PIN or passkey required for BLE
⚠️ **HTTP Only**: No HTTPS/TLS encryption
⚠️ **No Auth**: Web interface has no login
⚠️ **Always Broadcasting**: BLE continuously advertises

### Recommendations for Production Use

1. **Disable BLE After Setup**:
   ```cpp
   // In main.cpp setup(), add after delay:
   delay(300000);  // 5 minutes
   wifiAP.stopBLE();
   ```

2. **BLE Pairing** (Advanced):
   - Implement PIN-based BLE pairing
   - Require passkey for characteristic read

3. **HTTPS** (Advanced):
   - Generate self-signed certificate
   - Use ESPAsyncWebServer HTTPS

4. **Web Authentication**:
   - Add username/password to web interface
   - Use HTTP Basic Auth or session cookies

5. **Physical Security**:
   - Add button to enable/disable BLE
   - LED indicator for active connections

### Risk Assessment

| Scenario | Risk | Mitigation |
|----------|------|------------|
| Local Network Use | Low | Current setup adequate |
| Public Demo | Medium | Disable BLE after setup |
| Production Device | High | Implement all recommendations |
| IoT Fleet | Very High | Custom provisioning app + encryption |

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
