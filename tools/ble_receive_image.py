#!/usr/bin/env python3
"""
BLE Image Receiver Script for CyberGlass
Connects to CyberGlass device and receives images via BLE

Prerequisites:
    pip install bleak

    Or if using venv:
    source venv/bin/activate
    pip install bleak

Usage:
    python3 ble_receive_image.py [--output DIRECTORY]

Example:
    python3 ble_receive_image.py --output ./ble_images
"""

import asyncio
import sys
from datetime import datetime
from pathlib import Path
from bleak import BleakClient, BleakScanner

# BLE Service UUIDs
BLE_WIFI_SERVICE_UUID = "c153f40c-b1eb-11f0-bf8b-dbb9b632dc78"
BLE_IMAGE_SERVICE_UUID = "e3e6c300-b762-11f0-a4f8-d323d6ee8628"

# BLE Image Transfer Characteristic UUIDs
BLE_CHAR_IMAGE_REQUEST_UUID = "e3e6c310-b762-11f0-a4f8-d323d6ee8628"  # WRITE
BLE_CHAR_IMAGE_INFO_UUID = "f182b9d4-b762-11f0-8cab-7b33d60d040f"     # READ/NOTIFY
BLE_CHAR_IMAGE_DATA_UUID = "f5009d24-b762-11f0-9826-2f5155dc5a7b"     # READ/NOTIFY
BLE_CHAR_IMAGE_CONTROL_UUID = "f79a5a02-b762-11f0-9a55-0fae30ddfe0c"  # WRITE

# Image Transfer Configuration
BLE_IMAGE_CHUNK_SIZE = 480  # Bytes per chunk


class BLEImageReceiver:
    def __init__(self, output_dir="./ble_images"):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

        # Image transfer state
        self.image_size = 0
        self.total_chunks = 0
        self.received_chunks = {}
        self.transfer_active = False
        self.image_ready_event = asyncio.Event()
        self.chunk_received_event = asyncio.Event()

    async def find_device(self):
        """Scan for CyberGlass BLE device"""
        print("🔍 Scanning for CyberGlass devices...")
        devices = await BleakScanner.discover(timeout=10.0)

        cyberglass_devices = [d for d in devices if d.name and d.name.startswith("CyberGlass-")]

        if not cyberglass_devices:
            print("❌ No CyberGlass devices found!")
            return None

        print(f"\n✅ Found {len(cyberglass_devices)} device(s):")
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
                except (ValueError, KeyboardInterrupt):
                    return None

    def handle_image_info(self, sender, data):
        """Callback for image info notifications"""
        if len(data) < 7:
            print("❌ Invalid image info data")
            return

        status = data[0]
        self.image_size = int.from_bytes(data[1:5], byteorder='little')
        self.total_chunks = int.from_bytes(data[5:7], byteorder='little')

        status_str = {
            0: "Idle",
            1: "Ready",
            2: "Error",
            3: "Too Large",
            4: "Complete"
        }.get(status, f"Unknown({status})")

        print(f"\n📋 Image Info: Status={status_str}, Size={self.image_size} bytes, Chunks={self.total_chunks}")

        if status == 1:  # Ready
            self.transfer_active = True
            self.received_chunks = {}
            self.image_ready_event.set()
        elif status == 4:  # Complete
            print("✅ Transfer marked as complete by device")
            self.transfer_active = False

    def handle_image_data(self, sender, data):
        """Callback for image data chunk notifications"""
        if len(data) < 2:
            print("❌ Invalid chunk data (too short)")
            return

        # Parse chunk index (2 bytes, little-endian)
        chunk_index = int.from_bytes(data[0:2], byteorder='little')
        chunk_data = data[2:]

        # Store chunk
        self.received_chunks[chunk_index] = chunk_data

        print(f"📦 Received chunk {chunk_index + 1}/{self.total_chunks} ({len(chunk_data)} bytes)")

        self.chunk_received_event.set()

        # Check if we have all chunks
        if len(self.received_chunks) == self.total_chunks:
            print(f"✅ All {self.total_chunks} chunks received!")
            self.transfer_active = False

    async def request_image(self, client, resolution_index=1, quality=10):
        """Request image capture from device"""
        print(f"\n📸 Requesting image capture (resolution={resolution_index}, quality={quality})...")

        # Reset state
        self.transfer_active = False
        self.received_chunks = {}
        self.image_ready_event.clear()

        # Send request: [resolution_index, quality]
        request_data = bytes([resolution_index, quality])
        await client.write_gatt_char(BLE_CHAR_IMAGE_REQUEST_UUID, request_data)
        print("✅ Image request sent")

        # Wait for image info notification (with timeout)
        try:
            await asyncio.wait_for(self.image_ready_event.wait(), timeout=10.0)
        except asyncio.TimeoutError:
            print("❌ Timeout waiting for image info")
            return False

        return True

    async def receive_chunks(self, client):
        """Receive all image chunks"""
        print(f"\n📥 Receiving {self.total_chunks} chunks...")

        chunk_index = 1  # Start from chunk 1 (chunk 0 is sent automatically)

        while self.transfer_active and len(self.received_chunks) < self.total_chunks:
            # Check if we already have the next chunk
            if chunk_index in self.received_chunks:
                chunk_index += 1
                continue

            # Request next chunk: [command=1, chunk_low, chunk_high]
            control_data = bytes([
                1,  # Command: request chunk
                chunk_index & 0xFF,
                (chunk_index >> 8) & 0xFF
            ])

            await client.write_gatt_char(BLE_CHAR_IMAGE_CONTROL_UUID, control_data)

            # Wait for chunk (with timeout)
            self.chunk_received_event.clear()
            try:
                await asyncio.wait_for(self.chunk_received_event.wait(), timeout=5.0)
            except asyncio.TimeoutError:
                print(f"⚠️  Timeout waiting for chunk {chunk_index}, retrying...")
                continue

            chunk_index += 1

            # Small delay to prevent overwhelming the device
            await asyncio.sleep(0.05)

    def save_image(self):
        """Reassemble and save the complete image"""
        if len(self.received_chunks) != self.total_chunks:
            print(f"❌ Incomplete image: {len(self.received_chunks)}/{self.total_chunks} chunks")
            return None

        print(f"\n🔨 Reassembling image from {self.total_chunks} chunks...")

        # Reassemble image in order
        image_data = bytearray()
        for i in range(self.total_chunks):
            if i not in self.received_chunks:
                print(f"❌ Missing chunk {i}")
                return None
            image_data.extend(self.received_chunks[i])

        # Verify size
        if len(image_data) != self.image_size:
            print(f"⚠️  Size mismatch: expected {self.image_size}, got {len(image_data)}")

        # Generate filename
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"ble_image_{timestamp}.jpg"
        filepath = self.output_dir / filename

        # Save image
        with open(filepath, 'wb') as f:
            f.write(image_data)

        print(f"✅ Image saved: {filepath}")
        print(f"   Size: {len(image_data):,} bytes ({len(image_data)/1024:.2f} KB)")

        return filepath

    async def capture_and_receive(self, client, resolution_index=1, quality=10):
        """Complete workflow: request image and receive it"""
        # Request image
        if not await self.request_image(client, resolution_index, quality):
            return None

        # Receive all chunks
        await self.receive_chunks(client)

        # Save image
        return self.save_image()

    async def run(self):
        """Main execution flow"""
        print("=" * 70)
        print("    CyberGlass BLE Image Receiver")
        print("=" * 70)

        # Find device
        device = await self.find_device()
        if not device:
            return False

        print(f"\n📡 Connecting to {device.name}...")

        try:
            async with BleakClient(device.address) as client:
                print(f"✅ Connected to {device.name}!")

                # List all services and characteristics for debugging
                print("\n🔍 Discovering services and characteristics...")
                for service in client.services:
                    print(f"\n[Service] {service.uuid}")
                    for char in service.characteristics:
                        print(f"  └─ [Char] {char.uuid}")
                        print(f"      Properties: {char.properties}")

                # Check if image transfer characteristics exist
                print("\n🔍 Checking for image transfer characteristics...")
                has_image_request = any(char.uuid.lower() == BLE_CHAR_IMAGE_REQUEST_UUID.lower()
                                       for service in client.services for char in service.characteristics)
                has_image_info = any(char.uuid.lower() == BLE_CHAR_IMAGE_INFO_UUID.lower()
                                    for service in client.services for char in service.characteristics)
                has_image_data = any(char.uuid.lower() == BLE_CHAR_IMAGE_DATA_UUID.lower()
                                    for service in client.services for char in service.characteristics)
                has_image_control = any(char.uuid.lower() == BLE_CHAR_IMAGE_CONTROL_UUID.lower()
                                       for service in client.services for char in service.characteristics)

                print(f"  Image Request: {'✅' if has_image_request else '❌'}")
                print(f"  Image Info: {'✅' if has_image_info else '❌'}")
                print(f"  Image Data: {'✅' if has_image_data else '❌'}")
                print(f"  Image Control: {'✅' if has_image_control else '❌'}")

                if not all([has_image_request, has_image_info, has_image_data, has_image_control]):
                    print("\n❌ Image transfer characteristics not found!")
                    print("   Make sure you uploaded the latest firmware with BLE always enabled.")
                    return False

                # Subscribe to notifications
                print("\n🔔 Subscribing to image info notifications...")
                await client.start_notify(BLE_CHAR_IMAGE_INFO_UUID, self.handle_image_info)

                print("🔔 Subscribing to image data notifications...")
                await client.start_notify(BLE_CHAR_IMAGE_DATA_UUID, self.handle_image_data)

                print("✅ Notifications enabled")

                # Interactive loop
                print("\n" + "=" * 70)
                print("Commands:")
                print("  [Enter] or 'c' - Capture image")
                print("  'q' or 'exit'  - Quit")
                print("=" * 70)

                while True:
                    try:
                        user_input = input("\n> ").strip().lower()

                        if user_input in ['q', 'quit', 'exit']:
                            print("👋 Exiting...")
                            break
                        elif user_input == '' or user_input == 'c':
                            # Capture with SMALLEST resolution for testing (QQVGA = 160x120)
                            await self.capture_and_receive(client, resolution_index=0, quality=20)
                        else:
                            print("Unknown command. Press Enter to capture or 'q' to quit.")

                    except KeyboardInterrupt:
                        print("\n\n👋 Interrupted by user")
                        break

                # Unsubscribe
                await client.stop_notify(BLE_CHAR_IMAGE_INFO_UUID)
                await client.stop_notify(BLE_CHAR_IMAGE_DATA_UUID)

        except Exception as e:
            print(f"❌ Error: {e}")
            import traceback
            traceback.print_exc()
            return False

        return True


async def main():
    """Main entry point"""
    import argparse

    parser = argparse.ArgumentParser(description='Receive images from CyberGlass via BLE')
    parser.add_argument('--output', '-o', type=str, default='./ble_images',
                       help='Output directory for images (default: ./ble_images)')

    args = parser.parse_args()

    receiver = BLEImageReceiver(output_dir=args.output)
    await receiver.run()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n\n👋 Exiting...")
        sys.exit(0)
