#!/usr/bin/env python3
"""
Simple BLE write test to diagnose connection issues
"""

import asyncio
from bleak import BleakClient, BleakScanner

BLE_IMAGE_SERVICE_UUID = "e3e6c300-b762-11f0-a4f8-d323d6ee8628"
BLE_CHAR_IMAGE_REQUEST_UUID = "e3e6c310-b762-11f0-a4f8-d323d6ee8628"

async def test():
    print("Scanning for CyberGlass...")
    devices = await BleakScanner.discover(timeout=5.0)

    device = None
    for d in devices:
        if d.name and 'CyberGlass' in d.name:
            device = d
            print(f"Found: {device.name}")
            break

    if not device:
        print("No device found!")
        return

    print(f"\nConnecting to {device.name}...")

    async with BleakClient(device.address, timeout=20.0) as client:
        print(f"✅ Connected!")
        print(f"   Is connected: {client.is_connected}")

        # List all characteristics
        print("\n📋 All characteristics:")
        for service in client.services:
            print(f"\n[Service] {service.uuid}")
            for char in service.characteristics:
                props = ','.join(char.properties)
                print(f"  └─ {char.uuid} [{props}]")

        # Find the image request characteristic
        target_char = None
        for service in client.services:
            for char in service.characteristics:
                if char.uuid.lower() == BLE_CHAR_IMAGE_REQUEST_UUID.lower():
                    target_char = char
                    print(f"\n✅ Found target characteristic: {char.uuid}")
                    print(f"   Properties: {char.properties}")
                    print(f"   Service: {service.uuid}")
                    break

        if not target_char:
            print("\n❌ Target characteristic not found!")
            return

        # Test write
        print(f"\n🔧 Attempting to write...")
        print(f"   Client connected: {client.is_connected}")

        try:
            test_data = bytes([0, 20])  # resolution=0, quality=20
            print(f"   Writing {len(test_data)} bytes: {test_data.hex()}")

            await client.write_gatt_char(target_char, test_data, response=True)

            print("✅ Write successful!")

            # Wait a bit to see if ESP32 responds
            await asyncio.sleep(2)

        except Exception as e:
            print(f"❌ Write failed: {e}")
            print(f"   Client still connected: {client.is_connected}")

if __name__ == "__main__":
    asyncio.run(test())
