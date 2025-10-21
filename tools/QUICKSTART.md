# Quick Start - CyberGlass One-Click Connect

## One-Minute Quick Start

### 1. Setup Virtual Environment and Install Dependencies (First Time Only)

```bash
# Navigate to project root
cd /Users/simoncao/Capstone/CyberGlass

# Create virtual environment (if not already created)
python3 -m venv venv

# Activate virtual environment
source venv/bin/activate

# Install dependencies
pip install -r tools/requirements.txt
```

> **Tip**: After activation, you'll see `(venv)` in your command prompt

### 2. Run the Script

```bash
# Ensure virtual environment is activated (you should see (venv) in prompt)
cd tools
python ble_connect_and_open.py
```

That's it! The script will automatically:
1. ✅ Scan and find your CyberGlass device
2. ✅ Read WiFi password via BLE
3. ✅ Connect to the device's WiFi
4. ✅ Open the control interface in your browser

---

## If Permission Issues Occur

```bash
sudo python ble_connect_and_open.py
```

---

## Manual Connection Fallback

If automatic connection fails, the script will display WiFi information:

```
SSID: CyberGlass-XXXX
Password: xxxxxxxxxxxx
```

Then you can:
1. Manually connect to this WiFi network
2. Open in browser: http://192.168.4.1

---

## Full Documentation

See [README.md](README.md) for more detailed information.
