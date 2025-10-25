#!/usr/bin/env python3
"""
BLE WiFi Provisioning Script for CyberGlass
Sends WiFi credentials to the board via BLE
"""

import asyncio
import sys
from bleak import BleakClient, BleakScanner

# BLE Service and Characteristic UUIDs
SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
CHAR_EXT_SSID_UUID = "a3c87500-8ed3-4bdf-8a39-a01bebede295"
CHAR_EXT_PASSWORD_UUID = "a3c87501-8ed3-4bdf-8a39-a01bebede295"
CHAR_WIFI_STATUS_UUID = "a3c87502-8ed3-4bdf-8a39-a01bebede295"
CHAR_STA_IP_UUID = "a3c87503-8ed3-4bdf-8a39-a01bebede295"

async def find_cyberglass_device(device_name_prefix="CyberGlass"):
    """Scan for CyberGlass BLE devices"""
    print(f"Scanning for {device_name_prefix} devices...")
    devices = await BleakScanner.discover(timeout=5.0)

    cyberglass_devices = [d for d in devices if d.name and device_name_prefix in d.name]

    if not cyberglass_devices:
        print(f"No {device_name_prefix} devices found!")
        return None

    print(f"\nFound {len(cyberglass_devices)} device(s):")
    for i, device in enumerate(cyberglass_devices):
        print(f"  {i+1}. {device.name} ({device.address})")

    if len(cyberglass_devices) == 1:
        return cyberglass_devices[0]
    else:
        while True:
            try:
                choice = int(input(f"\nSelect device (1-{len(cyberglass_devices)}): "))
                if 1 <= choice <= len(cyberglass_devices):
                    return cyberglass_devices[choice - 1]
            except (ValueError, IndexError):
                print("Invalid choice. Try again.")

async def send_wifi_credentials(device_name=None, wifi_ssid=None, wifi_password=None):
    """Connect to CyberGlass and send WiFi credentials"""

    # Find the device
    if device_name:
        print(f"Looking for device: {device_name}")
        devices = await BleakScanner.discover(timeout=5.0)
        device = next((d for d in devices if d.name == device_name), None)
        if not device:
            print(f"Device {device_name} not found!")
            return False
    else:
        device = await find_cyberglass_device()
        if not device:
            return False

    print(f"\nConnecting to {device.name}...")

    try:
        async with BleakClient(device.address) as client:
            print(f"Connected to {device.name}!")

            # Check if paired
            print("\nNote: If pairing is required, enter PIN from serial monitor")

            # Get WiFi credentials from user if not provided
            if not wifi_ssid:
                wifi_ssid = input("\nEnter WiFi SSID (supports spaces): ")

            if not wifi_password:
                wifi_password = input("Enter WiFi Password: ")

            print(f"\n{'='*50}")
            print(f"Sending WiFi credentials:")
            print(f"  SSID: '{wifi_ssid}'")
            print(f"  SSID Length: {len(wifi_ssid)} characters")
            print(f"  Password Length: {len(wifi_password)} characters")
            print(f"{'='*50}\n")

            # Send SSID
            print("Sending SSID...")
            await client.write_gatt_char(CHAR_EXT_SSID_UUID, wifi_ssid.encode('utf-8'))
            print("✓ SSID sent successfully")

            await asyncio.sleep(0.5)

            # Send Password
            print("Sending password...")
            await client.write_gatt_char(CHAR_EXT_PASSWORD_UUID, wifi_password.encode('utf-8'))
            print("✓ Password sent successfully")

            # Wait for connection
            print("\nWaiting for board to connect to WiFi...")
            await asyncio.sleep(8)

            # Read status
            try:
                status_bytes = await client.read_gatt_char(CHAR_WIFI_STATUS_UUID)
                status = status_bytes.decode('utf-8')
                print(f"Connection Status: {status}")

                if "Connected" in status or "connected" in status:
                    # Read IP address
                    ip_bytes = await client.read_gatt_char(CHAR_STA_IP_UUID)
                    ip_address = ip_bytes.decode('utf-8')
                    print(f"\n{'='*50}")
                    print(f"SUCCESS! Board connected to WiFi")
                    print(f"IP Address: {ip_address}")
                    print(f"mDNS: http://cyberglass-{device.name.split('-')[1].lower()}.local")
                    print(f"{'='*50}")
                    return True
                else:
                    print(f"\n{'='*50}")
                    print(f"Connection may have failed. Check serial monitor for details.")
                    print(f"Status: {status}")
                    print(f"{'='*50}")
                    return False

            except Exception as e:
                print(f"Could not read status (board may still be connecting): {e}")
                print("Check serial monitor for connection status")
                return True

    except Exception as e:
        print(f"\nError: {e}")
        print("\nTroubleshooting:")
        print("1. Make sure the board is powered on")
        print("2. Check that BLE is enabled on your computer")
        print("3. If pairing fails, check serial monitor for PIN")
        print("4. Try running the script again")
        return False

async def main():
    """Main function"""
    print("=" * 60)
    print("CyberGlass BLE WiFi Provisioning Tool")
    print("=" * 60)

    # Parse command line arguments
    device_name = None
    wifi_ssid = None
    wifi_password = None

    if len(sys.argv) >= 2:
        device_name = sys.argv[1]
    if len(sys.argv) >= 3:
        wifi_ssid = sys.argv[2]
    if len(sys.argv) >= 4:
        wifi_password = sys.argv[3]

    success = await send_wifi_credentials(device_name, wifi_ssid, wifi_password)

    if success:
        print("\n✓ WiFi credentials sent successfully!")
        print("\nYou can now:")
        print("  1. Access the web interface via the IP address")
        print("  2. Use mDNS hostname (http://cyberglass-XXXX.local)")
        print("  3. Board will auto-connect on next boot")
    else:
        print("\n✗ Failed to configure WiFi")
        sys.exit(1)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n\nCancelled by user")
        sys.exit(0)
