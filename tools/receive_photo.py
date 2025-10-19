#!/usr/bin/env python3
"""
Photo Receiver Script for CyberGlass Camera
This script receives Base64 encoded photos from the serial port and saves them as JPEG files.

Usage:
    python receive_photo.py [--port PORT] [--baud BAUDRATE] [--output DIRECTORY]

Example:
    python receive_photo.py --port /dev/ttyACM0 --baud 115200 --output ./photos
"""

import serial
import serial.tools.list_ports
import base64
import argparse
from datetime import datetime
import os
import sys

def receive_photo(port, baudrate, output_dir):
    """
    Receive photos from serial port and save them
    
    Args:
        port: Serial port name (e.g., '/dev/ttyACM0' or 'COM3')
        baudrate: Baud rate (default: 115200)
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
        image_data = []
        last_data_time = time.time()
        
        def check_serial_output():
            """Check and print serial output"""
            nonlocal receiving, image_data, last_data_time
            
            while ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                last_data_time = time.time()
                
                # Print all serial output (except when receiving image data)
                if line and not receiving and '---BEGIN_IMAGE---' not in line:
                    print(line)
                
                # Start receiving image data
                if '---BEGIN_IMAGE---' in line:
                    receiving = True
                    image_data = []
                    print("\n� Receiving image data...")
                    continue
                
                # Stop receiving and save image
                if '---END_IMAGE---' in line and receiving:
                    receiving = False
                    print("📦 Decoding image...")
                    
                    try:
                        # Decode Base64 data
                        base64_str = ''.join(image_data)
                        
                        if not base64_str:
                            print("❌ Error: No image data received\n")
                            return
                        
                        image_bytes = base64.b64decode(base64_str)
                        
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
                        print(f"❌ Error saving photo: {e}")
                        print(f"   Received {len(base64_str)} bytes of Base64 data\n")
                    
                    return
                
                # Collect image data
                if receiving and line:
                    image_data.append(line)
            
            # Check if we're stuck receiving (no data for 3 seconds)
            if receiving and (time.time() - last_data_time > 3):
                print(f"⚠️  Warning: No data received for 3 seconds")
                print(f"   Received {len(image_data)} lines so far...")
                last_data_time = time.time()  # Reset to avoid spam
        
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
                    ser.flush()  # Ensure command is sent
                    time.sleep(0.1)
                elif user_input == '' or user_input in ['send', 'd', 'capture', 'c']:
                    # Empty input (just ENTER) or explicit capture command
                    print("📸 Capturing photo...")
                    ser.write(b'send\n')
                    ser.flush()  # Ensure command is sent
                    time.sleep(0.1)
                else:
                    # Send custom command
                    print(f"📤 Sending command: {user_input}")
                    ser.write(f'{user_input}\n'.encode())
                    ser.flush()  # Ensure command is sent
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
        default=115200,
        help='Baud rate (default: 115200)'
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
