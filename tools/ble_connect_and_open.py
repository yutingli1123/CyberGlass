#!/usr/bin/env python3
"""
CyberGlass BLE Auto-Connect Script (macOS)

Features:
1. Scan for nearby CyberGlass BLE devices
2. Connect to device and read WiFi credentials (SSID and password)
3. Automatically connect to the device's WiFi AP
4. Open the device's web interface

Dependencies:
pip install bleak

Usage:
python3 ble_connect_and_open.py
"""

import asyncio
import subprocess
import webbrowser
import time
from bleak import BleakScanner, BleakClient

# CyberGlass BLE UUIDs
SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
SSID_CHAR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
PASSWORD_CHAR_UUID = "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"

# Web interface URL
WEB_URL = "http://192.168.4.1"


class CyberGlassConnector:
    def __init__(self):
        self.ssid = None
        self.password = None
        self.device_address = None

    async def scan_devices(self, timeout=10):
        """Scan for nearby CyberGlass devices"""
        print(f"🔍 Scanning for CyberGlass devices... (timeout: {timeout}s)")

        devices = await BleakScanner.discover(timeout=timeout)

        cyberglass_devices = []
        for device in devices:
            if device.name and device.name.startswith("CyberGlass-"):
                cyberglass_devices.append(device)
                print(f"   ✓ Found device: {device.name} ({device.address})")

        if not cyberglass_devices:
            print("❌ No CyberGlass devices found")
            return None

        # Return the first device found
        return cyberglass_devices[0]

    async def read_credentials(self, device):
        """Connect to device and read WiFi credentials"""
        print(f"\n📡 Connecting to device: {device.name}")

        try:
            async with BleakClient(device.address) as client:
                print("   ✓ BLE connection successful")

                # Read SSID
                ssid_bytes = await client.read_gatt_char(SSID_CHAR_UUID)
                self.ssid = ssid_bytes.decode('utf-8')
                print(f"   ✓ SSID: {self.ssid}")

                # Read password
                password_bytes = await client.read_gatt_char(PASSWORD_CHAR_UUID)
                self.password = password_bytes.decode('utf-8')
                print(f"   ✓ Password: {self.password}")

                self.device_address = device.address

                print("✅ WiFi credentials retrieved successfully")
                return True

        except Exception as e:
            print(f"❌ Failed to read credentials: {e}")
            return False

    def connect_wifi_macos(self):
        """Connect to WiFi using macOS commands"""
        print(f"\n📶 Connecting to WiFi: {self.ssid}")

        try:
            # Get WiFi interface name (usually en0)
            interface_cmd = "networksetup -listallhardwareports | grep -A 1 Wi-Fi | grep Device | awk '{print $2}'"
            interface = subprocess.check_output(interface_cmd, shell=True).decode('utf-8').strip()

            if not interface:
                interface = "en0"  # Default to en0

            print(f"   Using network interface: {interface}")

            # Turn off WiFi
            subprocess.run(['networksetup', '-setairportpower', interface, 'off'],
                         check=False, capture_output=True)
            time.sleep(1)

            # Turn on WiFi
            subprocess.run(['networksetup', '-setairportpower', interface, 'on'],
                         check=True, capture_output=True)
            time.sleep(2)

            # Connect to network
            # Note: networksetup may require password to be stored in keychain on some macOS versions
            connect_cmd = f'networksetup -setairportnetwork {interface} "{self.ssid}" "{self.password}"'
            result = subprocess.run(connect_cmd, shell=True, capture_output=True, text=True)

            if result.returncode == 0:
                print("   ✓ WiFi connection command executed successfully")
                time.sleep(3)  # Wait for connection to establish

                # Verify connection
                current_ssid = subprocess.check_output(
                    f'/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport -I | grep " SSID" | awk \'{{print $2}}\'',
                    shell=True
                ).decode('utf-8').strip()

                if current_ssid == self.ssid:
                    print(f"✅ Successfully connected to {self.ssid}")
                    return True
                else:
                    print(f"⚠️  Current connection: {current_ssid}, Expected: {self.ssid}")
                    return False
            else:
                print(f"❌ WiFi connection failed: {result.stderr}")
                print("\n💡 Tip: If connection fails, try these methods:")
                print(f"   1. Manually connect to WiFi: {self.ssid}")
                print(f"   2. Password: {self.password}")
                print("   3. Or run this script with sudo")
                return False

        except subprocess.CalledProcessError as e:
            print(f"❌ Failed to execute WiFi connection command: {e}")
            print(f"\n💡 Please connect to WiFi manually:")
            print(f"   SSID: {self.ssid}")
            print(f"   Password: {self.password}")
            return False

    def open_web_interface(self):
        """Open device web interface"""
        print(f"\n🌐 Opening web interface: {WEB_URL}")

        # Wait for network to fully establish
        time.sleep(2)

        # Test connection
        try:
            import urllib.request
            urllib.request.urlopen(WEB_URL, timeout=5)
            print("   ✓ Device is accessible")
        except Exception as e:
            print(f"   ⚠️  Cannot access device: {e}")
            print("   You may need to wait a few seconds and access manually")

        # Open browser
        webbrowser.open(WEB_URL)
        print(f"✅ Browser opened: {WEB_URL}")

    async def run(self):
        """Main execution flow"""
        print("=" * 60)
        print("    CyberGlass Auto-Connect Tool (macOS)")
        print("=" * 60)

        # 1. Scan for devices
        device = await self.scan_devices()
        if not device:
            return False

        # 2. Read credentials
        success = await self.read_credentials(device)
        if not success:
            return False

        # 3. Connect to WiFi
        wifi_connected = self.connect_wifi_macos()

        # 4. Open web page (try even if WiFi connection fails, might be manually connected)
        self.open_web_interface()

        print("\n" + "=" * 60)
        if wifi_connected:
            print("✅ All steps completed!")
        else:
            print("⚠️  Some steps require manual action")
            print(f"\nIf WiFi didn't connect automatically, please connect manually:")
            print(f"  SSID: {self.ssid}")
            print(f"  Password: {self.password}")
            print(f"\nThen visit: {WEB_URL}")
        print("=" * 60)

        return True


async def main():
    connector = CyberGlassConnector()
    await connector.run()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n\n⚠️  Interrupted by user")
    except Exception as e:
        print(f"\n❌ Error occurred: {e}")
        import traceback
        traceback.print_exc()
