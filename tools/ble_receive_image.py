#!/usr/bin/env python3
"""
BLE Image Receiver for CyberGlass Camera
Receives images via BLE using the Image Transfer service.

Usage:
    python ble_receive_image.py [--device DEVICE_NAME] [--output DIRECTORY]

Example:
    python ble_receive_image.py --device CyberGlass-F2BC --output ./photos
"""

import argparse
import asyncio
import os
import sys
import time
from bleak import BleakClient, BleakScanner
from datetime import datetime

# BLE Service and Characteristic UUIDs
IMAGE_SERVICE_UUID = "c6116a0a-b7a0-11f0-880d-6baf85e562fd"  # Control service
IMAGE_DATA_SERVICE_1_UUID = "d8227b1c-c1d5-11f0-9f3e-4c6a95f7e8d1"  # Data service 1 (channels 1-4)
IMAGE_DATA_SERVICE_2_UUID = "e9338c2d-d2e6-11f0-a04f-5d7b0618f9e2"  # Data service 2 (channels 5-8)
IMAGE_REQUEST_UUID = "e3e6c310-b762-11f0-a4f8-d323d6ee8628"
IMAGE_INFO_UUID = "f182b9d4-b762-11f0-8cab-7b33d60d040f"
IMAGE_DATA_1_UUID = "f5009d24-b762-11f0-9826-2f5155dc5a7b"  # Channel 1
IMAGE_DATA_2_UUID = "a8c72f3e-c1d4-11f0-b2a5-9f4e61bc8d2a"  # Channel 2
IMAGE_DATA_3_UUID = "b3d84a52-c1d4-11f0-8f7c-1a5d92e3c4b6"  # Channel 3
IMAGE_DATA_4_UUID = "bd9e5c68-c1d4-11f0-9e4d-3b7a84f5d2c9"  # Channel 4
IMAGE_DATA_5_UUID = "c84fa74e-d2e6-11f0-9b5e-8e9c1720a1d3"  # Channel 5
IMAGE_DATA_6_UUID = "d35fb85f-d2e6-11f0-ac6f-9fad2831b2e4"  # Channel 6
IMAGE_DATA_7_UUID = "de60c960-d2e6-11f0-bd70-a0be3942c3f5"  # Channel 7
IMAGE_DATA_8_UUID = "e971da71-d2e6-11f0-ce81-b1cf4a53d4e6"  # Channel 8
IMAGE_CONTROL_UUID = "f79a5a02-b762-11f0-9a55-0fae30ddfe0c"

# Number of parallel data channels
IMAGE_DATA_CHANNELS = 8


class BLEImageReceiver:
    def __init__(self, device_name, output_dir):
        self.device_name = device_name
        self.output_dir = output_dir
        self.client = None

        # Image transfer state
        self.image_size = 0
        self.total_chunks = 0
        self.received_chunks = {}
        self.transfer_active = False
        self.transfer_start_time = None
        self.first_chunk_time = None
        self.missing_chunks_requested = False

        os.makedirs(output_dir, exist_ok=True)

    async def find_device(self):
        """Scan for the CyberGlass device"""
        print(f"🔍 Scanning for device: {self.device_name}...")

        devices = await BleakScanner.discover(timeout=10.0)

        for device in devices:
            if device.name and self.device_name in device.name:
                print(f"✅ Found device: {device.name} ({device.address})")
                return device

        print(f"❌ Device '{self.device_name}' not found!")
        print("\n📋 Available devices:")
        for device in devices:
            if device.name:
                print(f"  - {device.name} ({device.address})")

        return None

    def image_info_callback(self, sender, data):
        """Handle image info notifications"""
        if len(data) < 7:
            return

        status = data[0]
        size = int.from_bytes(data[1:5], byteorder='little')
        chunks = int.from_bytes(data[5:7], byteorder='little')

        if status == 0:
            print("📭 Image transfer idle")
        elif status == 1:
            print(f"📸 Image ready: {size} bytes, {chunks} chunks")
            self.image_size = size
            self.total_chunks = chunks
            self.received_chunks = {}
            self.transfer_active = True
            self.transfer_start_time = time.time()
            self.first_chunk_time = None
            self.missing_chunks_requested = False
        elif status == 2:
            print("❌ Image capture error")
            self.transfer_active = False
        elif status == 3:
            print("❌ Image too large")
            self.transfer_active = False
        elif status == 4:
            print("✅ ESP32 reports transfer complete!")
            asyncio.create_task(self.verify_and_complete())

    def image_data_callback(self, sender, data):
        """Handle image data chunk notifications"""
        if not self.transfer_active or len(data) < 2:
            return

        # Record first chunk time
        if self.first_chunk_time is None:
            self.first_chunk_time = time.time()

        chunk_index = int.from_bytes(data[0:2], byteorder='little')
        chunk_data = data[2:]

        self.received_chunks[chunk_index] = chunk_data

        # Calculate progress and speed
        progress = len(self.received_chunks) / self.total_chunks * 100
        elapsed = time.time() - self.first_chunk_time if self.first_chunk_time else 0

        if elapsed > 0:
            speed = len(self.received_chunks) / elapsed  # chunks per second
            eta = (self.total_chunks - len(self.received_chunks)) / speed if speed > 0 else 0
            print(
                f"📦 Chunk {chunk_index + 1}/{self.total_chunks} ({progress:.1f}%) | {speed:.1f} chunks/s | ETA: {eta:.1f}s",
                end='\r')
        else:
            print(f"📦 Chunk {chunk_index + 1}/{self.total_chunks} ({progress:.1f}%)", end='\r')

        # No need to request next chunks - ESP32 auto-sends all chunks

    async def request_chunk(self, chunk_index):
        """Request a specific chunk (for retransmit)"""
        if not self.client or not self.transfer_active:
            return

        try:
            if not self.client or not self.client.is_connected:
                return

            command = bytes([1, chunk_index & 0xFF, (chunk_index >> 8) & 0xFF])
            await self.client.write_gatt_char(IMAGE_CONTROL_UUID, command, response=False)
        except Exception as e:
            print(f"\n❌ Error requesting chunk {chunk_index}: {e}")

    async def verify_and_complete(self):
        """Verify integrity, request missing chunks, and save image"""
        if not self.transfer_active:
            return

        # Give last few notifications time to arrive
        await asyncio.sleep(0.1)

        # Check for missing chunks
        missing = []
        for i in range(self.total_chunks):
            if i not in self.received_chunks:
                missing.append(i)

        if missing and not self.missing_chunks_requested:
            print(f"\n⚠️  Missing {len(missing)} chunks: {missing[:10]}{'...' if len(missing) > 10 else ''}")
            print(f"📡 Requesting retransmit...")
            self.missing_chunks_requested = True

            # Request all missing chunks
            for chunk_idx in missing:
                await self.request_chunk(chunk_idx)
                await asyncio.sleep(0.01)  # Small delay to avoid congestion

            # Wait for retransmit, then verify again
            await asyncio.sleep(0.5)
            await self.verify_and_complete()  # Recursive check

        elif missing:
            print(f"\n❌ Still missing chunks after retransmit: {missing}")
            self.transfer_active = False

        else:
            # All chunks received!
            print(f"\n✅ All {self.total_chunks} chunks received!")
            await self.save_image()

    async def save_image(self):
        """Reconstruct and save the image from chunks"""
        if not self.transfer_active:
            return

        print("\n💾 Saving image...")

        # Calculate timing
        total_time = time.time() - self.transfer_start_time if self.transfer_start_time else 0
        transfer_time = time.time() - self.first_chunk_time if self.first_chunk_time else 0

        # Reconstruct image
        image_data = bytearray()
        for i in range(self.total_chunks):
            if i not in self.received_chunks:
                print(f"❌ Missing chunk {i}")
                return
            image_data.extend(self.received_chunks[i])

        # Trim to actual size
        image_data = image_data[:self.image_size]

        # Generate filename
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"ble_photo_{timestamp}.jpg"
        filepath = os.path.join(self.output_dir, filename)

        # Save image
        with open(filepath, 'wb') as f:
            f.write(image_data)

        file_size = len(image_data)
        throughput = file_size / transfer_time if transfer_time > 0 else 0

        print(f"✅ Photo saved: {filepath}")
        print(f"   Size: {file_size:,} bytes ({file_size / 1024:.2f} KB)")
        print(f"   Chunks: {len(self.received_chunks)}/{self.total_chunks}")
        print(f"   ⏱️  Total time: {total_time:.2f}s (capture + transfer)")
        print(f"   📶 Transfer time: {transfer_time:.2f}s")
        print(f"   🚀 Throughput: {throughput / 1024:.2f} KB/s ({throughput * 8 / 1024:.2f} kbps)")
        print(f"   📅 Timestamp: {timestamp}\n")

        # Send confirmation to ESP32 to release buffer
        try:
            if self.client and self.client.is_connected:
                await self.client.write_gatt_char(IMAGE_CONTROL_UUID, bytes([2]), response=False)
                print("📤 Sent confirmation to ESP32")
        except Exception as e:
            print(f"⚠️  Could not send confirmation: {e}")

        self.transfer_active = False
        self.received_chunks = {}

    async def request_image(self, resolution_index=1, quality=10):
        """Request image capture

        Args:
            resolution_index: 0=QQVGA, 1=QVGA, 2=VGA, 3=SVGA, 4=XGA, 5=HD, 6=SXGA, 7=UXGA
            quality: JPEG quality (0-63, lower is higher quality)
        """
        if not self.client:
            print("❌ Not connected to device")
            return

        resolution_names = ["QQVGA", "QVGA", "VGA", "SVGA", "XGA", "HD", "SXGA", "UXGA"]
        res_name = resolution_names[resolution_index] if resolution_index < len(
            resolution_names) else f"Index{resolution_index}"

        print(f"📸 Requesting image ({res_name}, quality={quality})...")
        print(f"⏱️  Capture started at {datetime.now().strftime('%H:%M:%S')}")

        try:
            # Check if still connected
            if not self.client or not self.client.is_connected:
                print("❌ Not connected to device")
                return

            request_data = bytes([resolution_index, quality])
            await self.client.write_gatt_char(IMAGE_REQUEST_UUID, request_data, response=False)
            print("✅ Request sent successfully")
        except Exception as e:
            print(f"❌ Error requesting image: {e}")
            print(f"   Connection status: {self.client.is_connected if self.client else 'No client'}")

            # Try to get more info
            try:
                services = self.client.services
                print(f"   Services available: {len(services)}")
            except:
                pass

    async def cancel_transfer(self):
        """Cancel current image transfer"""
        if not self.client:
            return

        print("🛑 Cancelling transfer...")

        try:
            if not self.client or not self.client.is_connected:
                return

            await self.client.write_gatt_char(IMAGE_CONTROL_UUID, bytes([0]), response=False)
            self.transfer_active = False
            self.received_chunks = {}
        except Exception as e:
            print(f"❌ Error cancelling transfer: {e}")

    async def connect_and_run(self):
        """Main connection and interaction loop"""
        device = await self.find_device()
        if not device:
            return

        print(f"🔌 Connecting to {device.name}...")

        async with BleakClient(device.address, timeout=20.0) as client:
            self.client = client

            print(f"✅ Connected!")

            # Verify services are available
            print("🔍 Discovering services...")

            image_control_service = None
            image_data_service_1 = None
            image_data_service_2 = None

            for service in client.services:
                if service.uuid.lower() == IMAGE_SERVICE_UUID.lower():
                    image_control_service = service
                    print(f"✅ Found Image Control service: {service.uuid}")
                elif service.uuid.lower() == IMAGE_DATA_SERVICE_1_UUID.lower():
                    image_data_service_1 = service
                    print(f"✅ Found Image Data service 1: {service.uuid}")
                elif service.uuid.lower() == IMAGE_DATA_SERVICE_2_UUID.lower():
                    image_data_service_2 = service
                    print(f"✅ Found Image Data service 2: {service.uuid}")

            if not image_control_service:
                print(f"❌ Image Control service not found!")
                print(f"   Expected UUID: {IMAGE_SERVICE_UUID}")
                print(f"\n📋 Available services:")
                for service in client.services:
                    print(f"   - {service.uuid}")
                    for char in service.characteristics:
                        print(f"     └─ {char.uuid} ({char.properties})")
                return

            if not image_data_service_1:
                print(f"❌ Image Data service 1 not found!")
                print(f"   Expected UUID: {IMAGE_DATA_SERVICE_1_UUID}")
                return

            if not image_data_service_2:
                print(f"❌ Image Data service 2 not found!")
                print(f"   Expected UUID: {IMAGE_DATA_SERVICE_2_UUID}")
                return

            # Verify all characteristics exist
            print("🔍 Verifying characteristics...")

            # Control service characteristics
            control_chars = {
                "Image Request": IMAGE_REQUEST_UUID,
                "Image Info": IMAGE_INFO_UUID,
                "Image Control": IMAGE_CONTROL_UUID
            }

            for name, uuid in control_chars.items():
                found = False
                for char in image_control_service.characteristics:
                    if char.uuid.lower() == uuid.lower():
                        print(f"   ✅ {name}: {char.uuid}")
                        found = True
                        break
                if not found:
                    print(f"   ❌ {name} not found in Control service!")
                    return

            # Data service 1 characteristics (channels 1-4)
            data_chars_1 = {
                "Image Data 1": IMAGE_DATA_1_UUID,
                "Image Data 2": IMAGE_DATA_2_UUID,
                "Image Data 3": IMAGE_DATA_3_UUID,
                "Image Data 4": IMAGE_DATA_4_UUID
            }

            for name, uuid in data_chars_1.items():
                found = False
                for char in image_data_service_1.characteristics:
                    if char.uuid.lower() == uuid.lower():
                        print(f"   ✅ {name}: {char.uuid}")
                        found = True
                        break
                if not found:
                    print(f"   ❌ {name} not found in Data service 1!")
                    return

            # Data service 2 characteristics (channels 5-8)
            data_chars_2 = {
                "Image Data 5": IMAGE_DATA_5_UUID,
                "Image Data 6": IMAGE_DATA_6_UUID,
                "Image Data 7": IMAGE_DATA_7_UUID,
                "Image Data 8": IMAGE_DATA_8_UUID
            }

            for name, uuid in data_chars_2.items():
                found = False
                for char in image_data_service_2.characteristics:
                    if char.uuid.lower() == uuid.lower():
                        print(f"   ✅ {name}: {char.uuid}")
                        found = True
                        break
                if not found:
                    print(f"   ❌ {name} not found in Data service 2!")
                    return

            # Subscribe to notifications
            print("🔔 Subscribing to notifications...")
            await client.start_notify(IMAGE_INFO_UUID, self.image_info_callback)
            # Subscribe to all 8 data channels for parallel transfer
            await client.start_notify(IMAGE_DATA_1_UUID, self.image_data_callback)
            await client.start_notify(IMAGE_DATA_2_UUID, self.image_data_callback)
            await client.start_notify(IMAGE_DATA_3_UUID, self.image_data_callback)
            await client.start_notify(IMAGE_DATA_4_UUID, self.image_data_callback)
            await client.start_notify(IMAGE_DATA_5_UUID, self.image_data_callback)
            await client.start_notify(IMAGE_DATA_6_UUID, self.image_data_callback)
            await client.start_notify(IMAGE_DATA_7_UUID, self.image_data_callback)
            await client.start_notify(IMAGE_DATA_8_UUID, self.image_data_callback)
            print("✅ Notifications enabled (1 info + 8 parallel data channels)!")

            print("\n🎮 Interactive Mode:")
            print("  Commands:")
            print("    'capture' or 'c' - Capture image (VGA, quality 10)")
            print("    'hq' - High quality capture (SVGA, quality 5)")
            print("    'lq' - Low quality capture (QVGA, quality 20)")
            print("    'cancel' - Cancel current transfer")
            print("    'exit' or 'q' - Quit")
            print()

            # Run interactive loop
            try:
                while True:
                    user_input = await asyncio.get_event_loop().run_in_executor(
                        None, input, ">> "
                    )

                    cmd = user_input.strip().lower()

                    if cmd in ['exit', 'q', 'quit']:
                        print("👋 Exiting...")
                        break
                    elif cmd in ['capture', 'c', '']:
                        await self.request_image(resolution_index=2, quality=10)  # VGA
                    elif cmd == 'hq':
                        await self.request_image(resolution_index=3, quality=5)  # SVGA
                    elif cmd == 'lq':
                        await self.request_image(resolution_index=1, quality=20)  # QVGA
                    elif cmd == 'cancel':
                        await self.cancel_transfer()
                    else:
                        print(f"❌ Unknown command: {cmd}")

                    await asyncio.sleep(0.1)

            except KeyboardInterrupt:
                print("\n👋 Exiting...")

            finally:
                await client.stop_notify(IMAGE_INFO_UUID)
                await client.stop_notify(IMAGE_DATA_1_UUID)
                await client.stop_notify(IMAGE_DATA_2_UUID)
                await client.stop_notify(IMAGE_DATA_3_UUID)
                await client.stop_notify(IMAGE_DATA_4_UUID)
                await client.stop_notify(IMAGE_DATA_5_UUID)
                await client.stop_notify(IMAGE_DATA_6_UUID)
                await client.stop_notify(IMAGE_DATA_7_UUID)
                await client.stop_notify(IMAGE_DATA_8_UUID)


async def main():
    parser = argparse.ArgumentParser(
        description='Receive images from CyberGlass camera via BLE'
    )
    parser.add_argument(
        '--device', '-d',
        type=str,
        default='CyberGlass',
        help='Device name to connect to (default: CyberGlass)'
    )
    parser.add_argument(
        '--output', '-o',
        type=str,
        default='./photos',
        help='Output directory for photos (default: ./photos)'
    )

    args = parser.parse_args()

    receiver = BLEImageReceiver(args.device, args.output)
    await receiver.connect_and_run()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n👋 Exiting...")
        sys.exit(0)
