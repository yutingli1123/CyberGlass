#!/usr/bin/env python3
"""
BLE Video Stream Test Script for CyberGlass

This script connects to a CyberGlass device via BLE and receives video stream.
Frames are displayed in real-time and optionally saved as JPEG images.

Press 'q' or 'ESC' in the video window to stop streaming.
Or use Ctrl+C in terminal.

Usage:
    python ble_video_stream_test.py [options]

Options:
    --resolution INDEX    Resolution index 0-7 (default: 2 for VGA)
    --quality VALUE       JPEG quality 10-63 (default: 50)
    --fps VALUE          Target FPS 1-10 (default: 5)
    --device-name PREFIX  Device name prefix (default: "CyberGlass")
    --output DIR         Output directory for saving frames (optional)
    --no-display         Disable real-time display window
"""

import argparse
import asyncio
import cv2
import numpy as np
import os
import sys
from bleak import BleakClient, BleakScanner
from datetime import datetime
from pathlib import Path

# BLE UUIDs
SERVICE_UUID = "503848c4-bce3-11f0-9ccd-bf30decea150"
IMAGE_INFO_UUID = "62fccb60-bce3-11f0-9a02-c38e72d2d0c8"
IMAGE_CONTROL_UUID = "82832b8c-bce3-11f0-bb48-cf7a2d9f36a2"
DATA_UUIDS = [
    "66f0e594-bce3-11f0-ac75-8b26179f0c8c",  # Channel 1
    "6accca16-bce3-11f0-aa05-17a54e5b82d7",  # Channel 2
    "6e12bdca-bce3-11f0-a24e-17df33e71289",  # Channel 3
    "716cb4e4-bce3-11f0-9bbc-838987d75d6a",  # Channel 4
    "74974f6c-bce3-11f0-8f1d-0f29ee6587b5",  # Channel 5
    "77c6710e-bce3-11f0-a955-638046cc804c",  # Channel 6
    "7b53517a-bce3-11f0-8118-7f24f2ff5f0f",  # Channel 7
    "7ee172c2-bce3-11f0-8828-574c4e3b235d",  # Channel 8
]

# Resolution names
RESOLUTION_NAMES = [
    "QQVGA (160x120)",
    "QVGA (320x240)",
    "VGA (640x480)",
    "SVGA (800x600)",
    "XGA (1024x768)",
    "HD (1280x720)",
    "SXGA (1280x1024)",
    "UXGA (1600x1200)",
]


class VideoStreamReceiver:
    """Handles BLE video stream reception and frame display/saving"""

    def __init__(self, output_dir=None, display=True):
        self.output_dir = Path(output_dir) if output_dir else None
        if self.output_dir:
            self.output_dir.mkdir(parents=True, exist_ok=True)

        self.display = display
        self.window_name = "CyberGlass Video Stream"
        self.should_stop = False

        self.current_frame_chunks = {}
        self.current_frame_number = 0
        self.current_total_chunks = 0
        self.frames_received = 0
        self.stream_active = False

        self.start_time = None
        self.last_frame_time = None

        # Create display window if enabled
        if self.display:
            cv2.namedWindow(self.window_name, cv2.WINDOW_NORMAL)
            cv2.resizeWindow(self.window_name, 800, 600)

    def info_notification_handler(self, sender, data):
        """Handle Image Info characteristic notifications"""
        status = data[0]

        if status == 5:
            # Video stream started
            res_idx = data[1]
            quality = data[2]
            fps = data[3]
            print(f"\n✓ Video stream started:")
            print(f"  Resolution: {RESOLUTION_NAMES[res_idx] if res_idx < 8 else 'Unknown'}")
            print(f"  Quality: {quality}")
            print(f"  Target FPS: {fps}")
            self.stream_active = True
            self.start_time = datetime.now()

        elif status == 6:
            # New video frame ready
            frame_count = int.from_bytes(data[1:5], 'little')
            total_chunks = int.from_bytes(data[5:7], 'little')

            # Save previous frame if we have all chunks
            if self.current_frame_chunks and len(self.current_frame_chunks) == self.current_total_chunks:
                self._save_frame()

            # Start new frame
            self.current_frame_number = frame_count
            self.current_total_chunks = total_chunks
            self.current_frame_chunks = {}

            current_time = datetime.now()
            if self.last_frame_time:
                elapsed = (current_time - self.last_frame_time).total_seconds()
                actual_fps = 1.0 / elapsed if elapsed > 0 else 0
                print(f"\rFrame {frame_count}: {total_chunks} chunks (FPS: {actual_fps:.1f})   ", end="", flush=True)
            else:
                print(f"\rFrame {frame_count}: {total_chunks} chunks", end="", flush=True)

            self.last_frame_time = current_time

        elif status == 0:
            # Stream stopped
            print("\n\n✓ Video stream stopped")
            self.stream_active = False

            # Save last frame if complete
            if self.current_frame_chunks and len(self.current_frame_chunks) == self.current_total_chunks:
                self._save_frame()

            # Print statistics
            if self.start_time:
                duration = (datetime.now() - self.start_time).total_seconds()
                avg_fps = self.frames_received / duration if duration > 0 else 0
                print(f"\nStatistics:")
                print(f"  Duration: {duration:.1f}s")
                print(f"  Frames received: {self.frames_received}")
                print(f"  Average FPS: {avg_fps:.2f}")
                if self.output_dir:
                    print(f"  Output directory: {self.output_dir}")

    def data_notification_handler(self, sender, data):
        """Handle Image Data characteristic notifications"""
        if not self.stream_active or len(data) < 2:
            return

        # Parse chunk
        chunk_index = int.from_bytes(data[0:2], 'little')
        chunk_data = bytes(data[2:])

        # Store chunk
        self.current_frame_chunks[chunk_index] = chunk_data

    def _save_frame(self):
        """Save and/or display current frame"""
        if not self.current_frame_chunks:
            return

        # Reassemble frame from chunks
        frame_data = bytearray()
        for i in range(self.current_total_chunks):
            if i in self.current_frame_chunks:
                frame_data.extend(self.current_frame_chunks[i])
            else:
                print(f"\n  Warning: Missing chunk {i}/{self.current_total_chunks}")
                return  # Don't process incomplete frame

        # Decode JPEG to image
        try:
            nparr = np.frombuffer(frame_data, np.uint8)
            img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

            if img is None:
                print(f"\n  Warning: Failed to decode frame {self.current_frame_number}")
                return

            # Mirror flip the image (horizontal flip)
            img = cv2.flip(img, 1)

            # Display frame
            if self.display:
                # Add frame info overlay
                frame_info = f"Frame: {self.current_frame_number} | FPS: {self._get_current_fps():.1f}"
                cv2.putText(img, frame_info, (10, 30), cv2.FONT_HERSHEY_SIMPLEX,
                            0.7, (0, 255, 0), 2)

                # Add control hint
                hint = "Press 'q' or ESC to stop"
                cv2.putText(img, hint, (10, 60), cv2.FONT_HERSHEY_SIMPLEX,
                            0.5, (0, 255, 255), 1)

                # Show image
                cv2.imshow(self.window_name, img)

                # Check for key press
                key = cv2.waitKey(1) & 0xFF
                if key == ord('q') or key == 27:  # 'q' or ESC
                    print("\n\nUser requested stop (keyboard)")
                    self.should_stop = True

            # Save to file if output directory specified (optional)
            if self.output_dir:
                filename = self.output_dir / f"frame_{self.current_frame_number:06d}.jpg"
                cv2.imwrite(str(filename), img)

            self.frames_received += 1

        except Exception as e:
            print(f"\n  Error processing frame: {e}")

    def _get_current_fps(self):
        """Calculate current FPS"""
        if self.last_frame_time and self.start_time:
            elapsed = (self.last_frame_time - self.start_time).total_seconds()
            if elapsed > 0 and self.frames_received > 0:
                return self.frames_received / elapsed
        return 0.0

    def check_keyboard(self):
        """Check for keyboard input (call this regularly even when no frames)"""
        if self.display:
            key = cv2.waitKey(1) & 0xFF
            if key == ord('q') or key == 27:  # 'q' or ESC
                print("\n\nUser requested stop (keyboard)")
                self.should_stop = True
                return True
        return False

    def cleanup(self):
        """Clean up resources"""
        if self.display:
            cv2.destroyAllWindows()


async def find_cyberglass_device(device_name_prefix="CyberGlass"):
    """Scan for CyberGlass device"""
    print(f"Scanning for BLE devices with name prefix '{device_name_prefix}'...")

    devices = await BleakScanner.discover(timeout=5.0)

    for device in devices:
        if device.name and device.name.startswith(device_name_prefix):
            print(f"✓ Found device: {device.name} ({device.address})")
            return device.address

    print(f"✗ No device found with name prefix '{device_name_prefix}'")
    return None


async def stream_video(address, resolution, quality, fps, chunk_delay, output_dir, display):
    """Connect to device and stream video"""

    receiver = VideoStreamReceiver(output_dir, display)

    print(f"\nConnecting to {address}...")
    async with BleakClient(address, timeout=20.0) as client:
        print(f"✓ Connected to {address}")

        # Subscribe to Image Info notifications
        print("Subscribing to Image Info notifications...")
        await client.start_notify(IMAGE_INFO_UUID, receiver.info_notification_handler)

        # Subscribe to all 8 Image Data channels
        print("Subscribing to Image Data channels...")
        for i, uuid in enumerate(DATA_UUIDS, 1):
            await client.start_notify(uuid, receiver.data_notification_handler)
            print(f"  Channel {i}/8 subscribed")

        # Start video stream
        print(f"\nStarting video stream...")
        print(f"  Resolution: {RESOLUTION_NAMES[resolution]}")
        print(f"  Quality: {quality}")
        print(f"  Target FPS: {fps}")
        print(f"  Chunk Delay: {chunk_delay}ms")

        command = bytes([3, resolution, quality, fps, chunk_delay])
        await client.write_gatt_char(IMAGE_CONTROL_UUID, command, response=False)

        # Stream continuously until stopped
        if display:
            print(f"\nStreaming video... (Press 'q' or ESC in video window to stop)")
        else:
            print(f"\nStreaming video... (Press Ctrl+C to stop)")

        try:
            # Keep running until user stops
            while not receiver.should_stop:
                # Check for keyboard input even if no frames are being received
                receiver.check_keyboard()
                await asyncio.sleep(0.1)
        except KeyboardInterrupt:
            print("\n\nUser requested stop (Ctrl+C)")

        # Stop video stream
        print("\nStopping video stream...")
        stop_command = bytes([4])
        await client.write_gatt_char(IMAGE_CONTROL_UUID, stop_command, response=False)

        # Wait for final frames
        await asyncio.sleep(1)

        # Cleanup
        receiver.cleanup()

        print("✓ Disconnected")


def main():
    parser = argparse.ArgumentParser(
        description="BLE Video Stream Test for CyberGlass",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Resolution Index:
  0 = QQVGA (160x120)
  1 = QVGA (320x240)
  2 = VGA (640x480)     [Default]
  3 = SVGA (800x600)
  4 = XGA (1024x768)
  5 = HD (1280x720)
  6 = SXGA (1280x1024)
  7 = UXGA (1600x1200)

Examples:
  # Stream VGA @ 5 FPS (stop with 'q' or Ctrl+C)
  python ble_video_stream_test.py

  # Stream QVGA @ 8 FPS
  python ble_video_stream_test.py --resolution 1 --fps 8

  # Stream VGA @ 3 FPS with high quality
  python ble_video_stream_test.py --fps 3 --quality 30

  # Stream and save frames
  python ble_video_stream_test.py --output ./my_frames
        """
    )

    parser.add_argument('--resolution', type=int, default=2, choices=range(8),
                        help='Resolution index 0-7 (default: 2 for VGA)')
    parser.add_argument('--quality', type=int, default=50,
                        help='JPEG quality 10-63, lower=better (default: 50)')
    parser.add_argument('--fps', type=int, default=5,
                        help='Target FPS 1-10 (default: 5)')
    parser.add_argument('--chunk-delay', type=int, default=50,
                        help='Chunk delay in ms 0-255 (default: 50)')
    parser.add_argument('--device-name', type=str, default="CyberGlass",
                        help='Device name prefix (default: "CyberGlass")')
    parser.add_argument('--output', type=str,
                        help='Output directory to save frames (optional, disabled by default)')
    parser.add_argument('--no-display', action='store_true',
                        help='Disable real-time display window')

    args = parser.parse_args()

    # Validate arguments
    if args.quality < 10 or args.quality > 63:
        print("Error: Quality must be between 10 and 63")
        sys.exit(1)

    if args.fps < 1 or args.fps > 10:
        print("Error: FPS must be between 1 and 10")
        sys.exit(1)

    if args.chunk_delay < 0 or args.chunk_delay > 255:
        print("Error: Chunk delay must be between 0 and 255")
        sys.exit(1)

    # Output directory (optional)
    output_dir = args.output

    # Display mode
    display = not args.no_display

    print("=" * 60)
    print("CyberGlass BLE Video Stream Test")
    print("=" * 60)
    if display:
        print("Mode: Real-time display" + (" + Save frames" if output_dir else ""))
    else:
        print("Mode: Save frames only" if output_dir else "Mode: No output")

    async def run():
        # Find device
        address = await find_cyberglass_device(args.device_name)
        if not address:
            print("\nPlease make sure:")
            print("  1. CyberGlass device is powered on")
            print("  2. Device is within BLE range (~10m)")
            print("  3. Device name starts with 'CyberGlass'")
            sys.exit(1)

        # Stream video
        try:
            await stream_video(
                address=address,
                resolution=args.resolution,
                quality=args.quality,
                fps=args.fps,
                chunk_delay=args.chunk_delay,
                output_dir=output_dir,
                display=display
            )
        except Exception as e:
            print(f"\n✗ Error: {e}")
            import traceback
            traceback.print_exc()
            sys.exit(1)

    asyncio.run(run())


if __name__ == "__main__":
    main()
