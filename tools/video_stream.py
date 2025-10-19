#!/usr/bin/env python3
"""
Real-time MJPEG Video Stream Viewer
Displays live video feed from XIAO ESP32S3 Sense camera via serial port
"""

import sys
import serial
import base64
import cv2
import numpy as np
import time
import threading
from io import BytesIO

class VideoStreamViewer:
    def __init__(self, port, baudrate=115200, target_fps=10):
        self.port = port
        self.baudrate = baudrate
        self.target_fps = target_fps
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
        print(f"Target FPS: {self.target_fps}")
        print("\nControls:")
        print("  - Press 'q' in video window to quit")
        print("  - Press 's' in video window to save current frame")
        print("  - Press '+' or '=' in video window to increase FPS")
        print("  - Press '-' or '_' in video window to decrease FPS")
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
            
            frame_interval = 1.0 / self.target_fps
            last_frame_time = time.time()
            
            while self.is_running:
                # Read frame data
                try:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                except:
                    continue
                
                if line == '---BEGIN_FRAME---':
                    # Collect Base64 data
                    base64_data = []
                    while True:
                        try:
                            data_line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                            if data_line == '---END_FRAME---':
                                break
                            if data_line:
                                base64_data.append(data_line)
                        except:
                            break
                    
                    # Decode and display frame
                    try:
                        base64_str = ''.join(base64_data)
                        if len(base64_str) > 0:
                            img_data = base64.b64decode(base64_str)
                            
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
                                cv2.putText(frame, f'Target FPS: {self.target_fps}', 
                                           (10, 150), cv2.FONT_HERSHEY_SIMPLEX, 
                                           1, (0, 255, 0), 2)
                                
                                # Display frame
                                cv2.imshow('ESP32 Camera Stream', frame)
                                
                                # Print progress
                                if self.frame_count % 30 == 0:
                                    print(f"Frames received: {self.frame_count}, FPS: {fps_display:.1f}")
                                
                    except Exception as e:
                        print(f"Error decoding frame: {e}")
                
                elif line:
                    # Print other messages from ESP32
                    print(f"ESP32: {line}")
                
                # Handle keyboard input (must be called regularly)
                key = cv2.waitKey(1) & 0xFF
                if key == ord('q') or key == 27:  # 'q' or ESC
                    print("\nStopping video stream...")
                    self.stop_stream()
                    break
                elif key == ord('s'):
                    self.save_frame()
                elif key == ord('+') or key == ord('='):
                    self.target_fps = min(self.target_fps + 1, 30)
                    frame_interval = 1.0 / self.target_fps
                    print(f"Target FPS increased to: {self.target_fps}")
                elif key == ord('-') or key == ord('_'):
                    self.target_fps = max(self.target_fps - 1, 1)
                    frame_interval = 1.0 / self.target_fps
                    print(f"Target FPS decreased to: {self.target_fps}")
            
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
        print("Usage: python3 video_stream.py <port> [fps]")
        print("Example: python3 video_stream.py /dev/cu.usbmodem14201 10")
        print("\nAvailable ports:")
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        for port in ports:
            print(f"  - {port.device}")
        sys.exit(1)
    
    port = sys.argv[1]
    fps = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    
    viewer = VideoStreamViewer(port, target_fps=fps)
    viewer.start_stream()

