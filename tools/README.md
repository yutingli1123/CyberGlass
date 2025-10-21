# CyberGlass Python Tools

This directory contains Python scripts for connecting to and controlling CyberGlass devices.

## Environment Setup

### First-Time Setup

This project uses Python virtual environment (venv) to manage dependencies.

```bash
# 1. Navigate to project root
cd /Users/simoncao/Capstone/CyberGlass

# 2. Create virtual environment (if not already created)
python3 -m venv venv

# 3. Activate virtual environment
source venv/bin/activate

# 4. Install dependencies for tools
pip install -r tools/requirements.txt
```

### Daily Usage

Activate the virtual environment before using the tools:

```bash
# Navigate to project root
cd /Users/simoncao/Capstone/CyberGlass

# Activate virtual environment
source venv/bin/activate

# Now you can run the tools
cd tools
python ble_connect_and_open.py
```

**Tip**: When the virtual environment is activated, you'll see `(venv)` in your command prompt.

### Dependencies

| Package | Purpose | Used By |
|---------|---------|---------|
| `bleak` | BLE communication | ble_connect_and_open.py |
| `pyserial` | Serial port communication | video_stream.py, fast_video_stream.py, receive_photo.py |
| `opencv-python` | Video display and image processing | video_stream.py, fast_video_stream.py |
| `numpy` | Numerical computing (OpenCV dependency) | video_stream.py, fast_video_stream.py |

---

## Available Tools

### 1. ble_connect_and_open.py - BLE Auto-Connect Tool

Automatically connects to CyberGlass device via BLE, retrieves WiFi credentials, connects to the AP, and opens the web interface.

**Features:**
- 🔍 Auto-scan for nearby CyberGlass BLE devices
- 📡 Connect to device and read WiFi SSID and password
- 📶 Automatically connect to device's WiFi AP (macOS)
- 🌐 Automatically open device's web control interface

**Usage:**

```bash
# Basic usage
python ble_connect_and_open.py

# If permission issues occur, use sudo
sudo python ble_connect_and_open.py
```

**Notes:**
- Requires Bluetooth permissions on macOS, will prompt on first run
- WiFi connection may require administrator privileges (sudo)
- If auto-connection fails, script will display SSID and password for manual connection

**Example Output:**

```
============================================================
    CyberGlass Auto-Connect Tool (macOS)
============================================================
🔍 Scanning for CyberGlass devices... (timeout: 10s)
   ✓ Found device: CyberGlass-A1B2 (XX:XX:XX:XX:XX:XX)

📡 Connecting to device: CyberGlass-A1B2
   ✓ BLE connection successful
   ✓ SSID: CyberGlass-A1B2
   ✓ Password: abc123xyz789
✅ WiFi credentials retrieved successfully

📶 Connecting to WiFi: CyberGlass-A1B2
   Using network interface: en0
   ✓ WiFi connection command executed successfully
✅ Successfully connected to CyberGlass-A1B2

🌐 Opening web interface: http://192.168.4.1
   ✓ Device is accessible
✅ Browser opened: http://192.168.4.1

============================================================
✅ All steps completed!
============================================================
```

---

### 2. video_stream.py - Video Stream Viewer

Displays live MJPEG video stream from CyberGlass via HTTP.

**Usage:**

```bash
python video_stream.py
```

**Features:**
- 📹 Real-time MJPEG video stream display
- ⌨️  Press 'q' to exit
- 🖼️  Press 's' to save screenshot

---

## Workflow

```
┌─────────────────────────────────────────────────────────┐
│  1. Run ble_connect_and_open.py                         │
│                                                          │
│  ┌────────────┐      ┌────────────┐      ┌──────────┐  │
│  │ BLE Scan   │─────>│Read Creds  │─────>│WiFi Conn │  │
│  └────────────┘      └────────────┘      └──────────┘  │
│                                                 │        │
│                                                 v        │
│                                          ┌──────────┐   │
│                                          │Open Web  │   │
│                                          └──────────┘   │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  2. Use web interface or video_stream.py to control     │
│     the camera                                          │
└─────────────────────────────────────────────────────────┘
```

---

## FAQ

### Q: BLE scan cannot find device?

A:
- Ensure ESP32 device is powered on and running
- Check that Bluetooth is enabled
- Ensure device is within Bluetooth range (< 10 meters)
- macOS: System Preferences -> Security & Privacy -> Privacy -> Bluetooth, allow Terminal access

### Q: WiFi connection failed?

A:
- Try running the script with sudo: `sudo python ble_connect_and_open.py`
- Or connect manually:
  1. Open WiFi settings
  2. Select CyberGlass-XXXX network
  3. Enter the password displayed by the script
  4. Manually visit http://192.168.4.1

### Q: Browser opened but cannot access?

A:
- Wait 3-5 seconds for WiFi connection to fully establish
- Manually refresh the browser page
- Confirm you're connected to the correct WiFi network
- Ping test: `ping 192.168.4.1`

### Q: macOS networksetup command failed?

A:
- Some macOS versions require WiFi password to be stored in keychain first
- Recommended to manually connect the first time, then use script for auto-connect
- Or use sudo: `sudo python ble_connect_and_open.py`

---

## Development and Debugging

### Enable Verbose Logging

Modify timeout or add debug output in the script:

```python
# In ble_connect_and_open.py
device = await self.scan_devices(timeout=20)  # Increase scan time
```

### Test BLE Connection

Use macOS built-in Bluetooth Explorer or third-party tools (like LightBlue) to verify BLE services.

### Test WiFi Connection

```bash
# Check current WiFi
/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport -I

# Manual connect
networksetup -setairportnetwork en0 "CyberGlass-XXXX" "password"
```

---

## Supported Platforms

- ✅ macOS 10.15+ (Catalina and above)
- ✅ Python 3.7+
- ⚠️  Linux support (requires modifications to WiFi connection section)
- ❌ Windows (requires separate WiFi connection implementation)

---

## License

Same as CyberGlass project
