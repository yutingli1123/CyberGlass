#!/usr/bin/env python3
"""
Real-time Binary Video Stream Viewer
Displays live video feed from XIAO ESP32S3 Sense camera via serial port using binary transfer
"""

import sys
import serial
import cv2
import numpy as np
import time
import threading
from io import BytesIO

class VideoStreamViewer:
    def __init__(self, port, baudrate=921600):
        self.port = port
        self.baudrate = baudrate
        self.is_running = False
        self.current_frame = None
        self.frame_count = 0
        self.saved_count = 0
        self.start_time = None
        self.ser = None
        
    def start_stream(self):
        """Start the video stream"""
        print(f"Opening serial port: {self.port}")
        print(f"Baud rate: {self.baudrate}")
        print("\nControls:")
        print("  - Press 'q' in video window to quit")
        print("  - Press 's' in video window to save current frame")
        print("\nStarting video stream...\n")
        
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            time.sleep(2)  # Wait for connection to stabilize
            
            # Start streaming mode
            print("Sending 'stream' command to ESP32...")
            self.ser.write(b'stream\n')
            self.ser.flush()
            time.sleep(0.5)
            
            self.is_running = True
            self.start_time = time.time()
            
            # Create window
            cv2.namedWindow('ESP32 Camera Stream', cv2.WINDOW_NORMAL)

            while self.is_running:
                # Read binary frame data
                try:
                    # Look for frame marker (0xC0 0x1D 0xF1 0x8E - "COLD F18E")
                    byte = self.ser.read(1)
                    if not byte:
                        continue

                    if byte[0] == 0xC0:
                        # Check for complete marker
                        marker = self.ser.read(3)
                        if len(marker) == 3 and marker == b'\x1D\xF1\x8E':
                            # Read frame size (4 bytes, little-endian)
                            size_bytes = self.ser.read(4)
                            if len(size_bytes) != 4:
                                continue

                            frame_size = int.from_bytes(size_bytes, byteorder='little')

                            # Check for end marker
                            if frame_size == 0xFFFFFFFF:
                                print("\nReceived end marker, stream stopped by device")
                                self.stop_stream()
                                break

                            # Validate frame size
                            if frame_size > 500000 or frame_size < 100:
                                print(f"Invalid frame size: {frame_size}, skipping")
                                continue

                            # Read frame data
                            img_data = self.ser.read(frame_size)
                            if len(img_data) != frame_size:
                                print(f"Incomplete frame: expected {frame_size}, got {len(img_data)}")
                                continue

                            # Decode and display frame
                            try:
                                # Decode JPEG
                                nparr = np.frombuffer(img_data, np.uint8)
                                frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

                                if frame is not None:
                                    self.current_frame = frame.copy()
                                    self.frame_count += 1

                                    # Calculate FPS
                                    current_time = time.time()
                                    elapsed = current_time - self.start_time
                                    fps_display = self.frame_count / elapsed if elapsed > 0 else 0

                                    # Add overlays
                                    cv2.putText(frame, f'FPS: {fps_display:.1f}',
                                               (10, 30), cv2.FONT_HERSHEY_SIMPLEX,
                                               1, (0, 255, 0), 2)
                                    cv2.putText(frame, f'Frame: {self.frame_count}',
                                               (10, 70), cv2.FONT_HERSHEY_SIMPLEX,
                                               1, (0, 255, 0), 2)
                                    cv2.putText(frame, f'Size: {len(img_data)/1024:.1f}KB',
                                               (10, 110), cv2.FONT_HERSHEY_SIMPLEX,
                                               1, (0, 255, 0), 2)

                                    # Display frame
                                    cv2.imshow('ESP32 Camera Stream', frame)

                                    # Print progress
                                    if self.frame_count % 30 == 0:
                                        print(f"Frames received: {self.frame_count}, FPS: {fps_display:.1f}")

                            except Exception as e:
                                print(f"Error decoding frame: {e}")

                    elif byte[0] == 0xFF:
                        # Check if this is end marker (0xFF 0xFF 0xFF 0xFF)
                        next_bytes = self.ser.read(3)
                        if len(next_bytes) == 3 and next_bytes == b'\xFF\xFF\xFF':
                            print("\nReceived end marker, stream stopped")
                            self.stop_stream()
                            break

                except Exception as e:
                    print(f"Error reading frame: {e}")
                    continue
                
                # Handle keyboard input (must be called regularly)
                key = cv2.waitKey(1) & 0xFF
                if key == ord('q') or key == 27:  # 'q' or ESC
                    print("\nStopping video stream...")
                    self.stop_stream()
                    break
                elif key == ord('s'):
                    self.save_frame()
            
            # Print statistics
            self.print_statistics()
            
        except serial.SerialException as e:
            print(f"Serial error: {e}")
            print("\nTroubleshooting:")
            print("  1. Make sure the device is connected")
            print("  2. Close any other programs using the serial port")
            print("  3. Try unplugging and reconnecting the device")
            sys.exit(1)
        except KeyboardInterrupt:
            print("\nInterrupted by user")
            self.stop_stream()
        finally:
            self.cleanup()
    
    def stop_stream(self):
        """Stop the video stream"""
        if self.ser and self.ser.is_open:
            try:
                self.ser.write(b'stop\n')
                self.ser.flush()
                time.sleep(0.5)
            except:
                pass
        self.is_running = False
    
    def save_frame(self):
        """Save current frame to file"""
        if self.current_frame is not None:
            self.saved_count += 1
            filename = f'frame_{int(time.time()*1000)}.jpg'
            cv2.imwrite(filename, self.current_frame)
            print(f"✓ Saved frame: {filename}")
        else:
            print("No frame to save")
    
    def print_statistics(self):
        """Print streaming statistics"""
        if self.start_time:
            elapsed = time.time() - self.start_time
            print(f"\n{'='*40}")
            print(f"Streaming Statistics:")
            print(f"  Total frames: {self.frame_count}")
            print(f"  Duration: {elapsed:.1f}s")
            if elapsed > 0:
                print(f"  Average FPS: {self.frame_count/elapsed:.2f}")
            print(f"  Frames saved: {self.saved_count}")
            print(f"{'='*40}\n")
    
    def cleanup(self):
        """Clean up resources"""
        if self.ser and self.ser.is_open:
            self.ser.close()
        cv2.destroyAllWindows()

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 video_stream.py <port>")
        print("Example: python3 video_stream.py /dev/cu.usbmodem14201")
        print("\nAvailable ports:")
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        for port in ports:
            print(f"  - {port.device}")
        sys.exit(1)

    port = sys.argv[1]

    viewer = VideoStreamViewer(port)
    viewer.start_stream()

