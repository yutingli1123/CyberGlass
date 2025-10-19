#!/usr/bin/env python3
"""
Photo Receiver Script for CyberGlass Camera
This script receives binary encoded photos from the serial port and saves them as JPEG files.

Usage:
    python receive_photo.py [--port PORT] [--baud BAUDRATE] [--output DIRECTORY]

Example:
    python receive_photo.py --port /dev/ttyACM0 --baud 921600 --output ./photos
"""

import serial
import serial.tools.list_ports
import argparse
from datetime import datetime
import os
import sys

def receive_photo(port, baudrate, output_dir):
    """
    Receive photos from serial port and save them

    Args:
        port: Serial port name (e.g., '/dev/ttyACM0' or 'COM3')
        baudrate: Baud rate (default: 921600)
        output_dir: Directory to save photos
    """
    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    print(f"Connecting to {port} at {baudrate} baud...")

    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"✅ Connected! Waiting for device to initialize...")

        # Wait a bit for device to stabilize
        import time
        time.sleep(2)

        # Clear any existing data
        ser.reset_input_buffer()

        print("\n🎮 Interactive Mode:")
        print("  - Press ENTER to capture a photo")
        print("  - Type 'status' or 's' to check camera status")
        print("  - Type 'exit' or 'q' to quit")
        print("  - Press Ctrl+C to exit\n")

        receiving = False
        last_data_time = time.time()

        def check_serial_output():
            """Check and print serial output, handle binary image data"""
            nonlocal receiving, last_data_time

            while ser.in_waiting > 0:
                # Try to read one byte to check for binary marker
                first_byte = ser.read(1)
                if not first_byte:
                    break

                # Check for binary frame marker (0xC0 0x1D 0xF1 0x8E - "COLD FIRE")
                if first_byte[0] == 0xC0 and ser.in_waiting >= 3:
                    marker = ser.read(3)
                    if len(marker) == 3 and marker == b'\x1D\xF1\x8E':
                        # Binary image incoming
                        receiving = True
                        print("\n📸 Receiving binary image data...")
                        last_data_time = time.time()

                        try:
                            # Read frame size (4 bytes, little-endian)
                            size_bytes = ser.read(4)
                            if len(size_bytes) != 4:
                                print("❌ Error: Failed to read image size")
                                receiving = False
                                continue

                            frame_size = int.from_bytes(size_bytes, byteorder='little')

                            # Validate frame size
                            if frame_size > 500000 or frame_size < 100:
                                print(f"❌ Error: Invalid frame size: {frame_size}")
                                receiving = False
                                continue

                            print(f"📦 Reading {frame_size} bytes...")

                            # Read image data
                            image_bytes = ser.read(frame_size)
                            if len(image_bytes) != frame_size:
                                print(f"❌ Error: Incomplete data (expected {frame_size}, got {len(image_bytes)})")
                                receiving = False
                                continue

                            # Generate filename with timestamp
                            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                            filename = f"photo_{timestamp}.jpg"
                            filepath = os.path.join(output_dir, filename)

                            # Save image
                            with open(filepath, 'wb') as f:
                                f.write(image_bytes)

                            file_size = len(image_bytes)
                            print(f"✅ Photo saved: {filepath}")
                            print(f"   Size: {file_size:,} bytes ({file_size/1024:.2f} KB)")
                            print(f"   Timestamp: {timestamp}\n")

                        except Exception as e:
                            print(f"❌ Error saving photo: {e}\n")

                        receiving = False
                        continue

                # Not a binary marker, try to decode as text
                try:
                    # Read the rest of the line
                    line = (first_byte + ser.readline()).decode('utf-8', errors='ignore').strip()
                    if line:
                        print(line)
                        last_data_time = time.time()
                except:
                    pass

        # Main interactive loop
        import select
        while True:
            # Check for serial output
            check_serial_output()

            # Check for user input (non-blocking on Unix-like systems)
            if sys.stdin in select.select([sys.stdin], [], [], 0)[0]:
                user_input = sys.stdin.readline().strip().lower()

                if user_input in ['exit', 'q', 'quit']:
                    print("\n👋 Exiting...")
                    break
                elif user_input in ['status', 's']:
                    print("📤 Sending 'status' command...")
                    ser.write(b'status\n')
                    ser.flush()
                    time.sleep(0.1)
                elif user_input == '' or user_input in ['send', 'd', 'capture', 'c']:
                    # Empty input (just ENTER) or explicit capture command
                    print("📸 Capturing photo...")
                    ser.write(b'send\n')
                    ser.flush()
                    time.sleep(0.1)
                else:
                    # Send custom command
                    print(f"📤 Sending command: {user_input}")
                    ser.write(f'{user_input}\n'.encode())
                    ser.flush()
                    time.sleep(0.1)

            # Small delay to prevent CPU hogging
            time.sleep(0.01)

    except serial.SerialException as e:
        print(f"❌ Serial port error: {e}")
        print("\nAvailable ports:")
        ports = serial.tools.list_ports.comports()
        for port in ports:
            print(f"  - {port.device}: {port.description}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n\n👋 Exiting...")
        ser.close()
        sys.exit(0)
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
        sys.exit(1)


def list_serial_ports():
    """List all available serial ports"""

    print("\n📋 Available Serial Ports:")
    ports = serial.tools.list_ports.comports()

    if not ports:
        print("  No serial ports found!")
        return

    for port in ports:
        print(f"  - {port.device}")
        print(f"    Description: {port.description}")
        print(f"    Hardware ID: {port.hwid}")
        print()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='Receive photos from CyberGlass camera via serial port'
    )
    parser.add_argument(
        '--port', '-p',
        type=str,
        help='Serial port (e.g., /dev/ttyACM0 or COM3)'
    )
    parser.add_argument(
        '--baud', '-b',
        type=int,
        default=921600,
        help='Baud rate (default: 921600)'
    )
    parser.add_argument(
        '--output', '-o',
        type=str,
        default='./photos',
        help='Output directory for photos (default: ./photos)'
    )
    parser.add_argument(
        '--list', '-l',
        action='store_true',
        help='List available serial ports and exit'
    )

    args = parser.parse_args()

    # List ports if requested
    if args.list:
        list_serial_ports()
        sys.exit(0)
    # Check if port is specified
    if not args.port:
        print("❌ Error: Serial port not specified!")
        print("\nUse --list to see available ports:")
        print("  python receive_photo.py --list")
        print("\nThen specify the port:")
        print("  python receive_photo.py --port /dev/ttyACM0")
        print("  python receive_photo.py --port COM3  (Windows)")
        sys.exit(1)
    # Start receiving photos
    receive_photo(args.port, args.baud, args.output)
