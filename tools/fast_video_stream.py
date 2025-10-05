#!/usr/bin/env python3
"""
Fast Binary Video Stream Viewer
High-speed video feed from XIAO ESP32S3 Sense using binary transfer
"""

import sys
import serial
import cv2
import numpy as np
import time
import struct

class FastVideoStreamViewer:
    def __init__(self, port, baudrate=921600, target_fps=30):
        self.port = port
        self.baudrate = baudrate
        self.target_fps = target_fps
        self.is_running = False
        self.current_frame = None
        self.frame_count = 0
        self.saved_count = 0
        self.start_time = None
        self.ser = None
        self.total_bytes = 0
        
    def read_exact(self, size):
        """Read exact number of bytes from serial"""
        data = b''
        while len(data) < size:
            chunk = self.ser.read(size - len(data))
            if not chunk:
                return None
            data += chunk
        return data
    
    def find_frame_marker(self):
        """Find frame start marker in stream"""
        marker = b'\xAA\xBB\xCC\xDD'
        error_marker = b'\xFF\xFF\xFF\xFF'
        buffer = b''
        
        while len(buffer) < 4:
            byte = self.ser.read(1)
            if not byte:
                continue
            buffer += byte
            if len(buffer) > 4:
                buffer = buffer[-4:]
            
            if buffer == marker:
                return True
            elif buffer == error_marker:
                return False
        
        return False
    
    def start_stream(self):
        """Start the binary video stream"""
        print(f"╔{'═'*50}╗")
        print(f"║ Fast Binary Video Stream Viewer{' '*18}║")
        print(f"╠{'═'*50}╣")
        print(f"║ Port: {self.port:<43}║")
        print(f"║ Baud rate: {self.baudrate:<38}║")
        print(f"║ Target FPS: {self.target_fps:<37}║")
        print(f"╠{'═'*50}╣")
        print(f"║ Controls:{' '*40}║")
        print(f"║   q or ESC - Quit{' '*30}║")
        print(f"║   s        - Save current frame{' '*18}║")
        print(f"║   +/=      - Increase target FPS{' '*16}║")
        print(f"║   -/_      - Decrease target FPS{' '*16}║")
        print(f"╚{'═'*50}╝")
        print("\nStarting binary video stream...\n")
        
        try:
            # Open serial port with high baud rate
            self.ser = serial.Serial(self.port, self.baudrate, timeout=2)
            time.sleep(2)  # Wait for connection
            
            # Clear any pending data
            self.ser.reset_input_buffer()
            
            # Start binary streaming mode
            print("Sending 'bstream' command to ESP32...")
            self.ser.write(b'bstream\n')
            self.ser.flush()
            time.sleep(1)
            
            self.is_running = True
            self.start_time = time.time()
            
            # Create window
            cv2.namedWindow('ESP32 Fast Camera Stream', cv2.WINDOW_NORMAL)
            
            last_print_time = time.time()
            
            while self.is_running:
                # Find frame marker
                if not self.find_frame_marker():
                    continue
                
                # Read frame size (4 bytes, little-endian)
                size_bytes = self.read_exact(4)
                if not size_bytes:
                    continue
                
                frame_size = struct.unpack('<I', size_bytes)[0]
                
                # Validate frame size
                if frame_size > 500000 or frame_size < 100:
                    print(f"Warning: Invalid frame size: {frame_size}")
                    continue
                
                # Read frame data
                frame_data = self.read_exact(frame_size)
                if not frame_data or len(frame_data) != frame_size:
                    print(f"Warning: Incomplete frame data")
                    continue
                
                self.total_bytes += frame_size
                
                # Decode JPEG
                try:
                    nparr = np.frombuffer(frame_data, np.uint8)
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
                        cv2.putText(frame, f'Size: {frame_size/1024:.1f}KB', 
                                   (10, 110), cv2.FONT_HERSHEY_SIMPLEX, 
                                   1, (0, 255, 0), 2)
                        cv2.putText(frame, f'Speed: {self.baudrate}', 
                                   (10, 150), cv2.FONT_HERSHEY_SIMPLEX, 
                                   1, (0, 255, 0), 2)
                        cv2.putText(frame, 'BINARY MODE', 
                                   (10, 190), cv2.FONT_HERSHEY_SIMPLEX, 
                                   1, (0, 255, 255), 2)
                        
                        # Display frame
                        cv2.imshow('ESP32 Fast Camera Stream', frame)
                        
                        # Print progress every 2 seconds
                        if current_time - last_print_time > 2.0:
                            bandwidth = (self.total_bytes / elapsed) / 1024  # KB/s
                            print(f"Frames: {self.frame_count}, FPS: {fps_display:.1f}, "
                                  f"Bandwidth: {bandwidth:.1f} KB/s")
                            last_print_time = current_time
                        
                except Exception as e:
                    print(f"Error decoding frame: {e}")
                    continue
                
                # Handle keyboard input
                key = cv2.waitKey(1) & 0xFF
                if key == ord('q') or key == 27:  # 'q' or ESC
                    print("\nStopping video stream...")
                    self.stop_stream()
                    break
                elif key == ord('s'):
                    self.save_frame()
                elif key == ord('+') or key == ord('='):
                    self.target_fps = min(self.target_fps + 5, 60)
                    print(f"Target FPS: {self.target_fps}")
                elif key == ord('-') or key == ord('_'):
                    self.target_fps = max(self.target_fps - 5, 5)
                    print(f"Target FPS: {self.target_fps}")
            
            # Print statistics
            self.print_statistics()
            
        except serial.SerialException as e:
            print(f"\n❌ Serial error: {e}")
            print("\nTroubleshooting:")
            print("  1. Make sure the device is connected")
            print("  2. Close any other programs using the serial port")
            print("  3. Upload code with new baud rate (921600)")
            print("  4. Try unplugging and reconnecting the device")
            sys.exit(1)
        except KeyboardInterrupt:
            print("\n\nInterrupted by user")
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
            bandwidth = (self.total_bytes / elapsed) / 1024  # KB/s
            print(f"\n{'='*60}")
            print(f"{'Streaming Statistics':^60}")
            print(f"{'='*60}")
            print(f"  Total frames:     {self.frame_count}")
            print(f"  Duration:         {elapsed:.1f}s")
            print(f"  Average FPS:      {self.frame_count/elapsed:.2f}")
            print(f"  Total data:       {self.total_bytes/1048576:.2f} MB")
            print(f"  Average bandwidth: {bandwidth:.1f} KB/s")
            print(f"  Frames saved:     {self.saved_count}")
            print(f"{'='*60}\n")
    
    def cleanup(self):
        """Clean up resources"""
        if self.ser and self.ser.is_open:
            self.ser.close()
        cv2.destroyAllWindows()

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 fast_video_stream.py <port> [fps]")
        print("Example: python3 fast_video_stream.py /dev/cu.usbmodem14201 30")
        print("\nThis uses BINARY transfer mode for maximum speed!")
        print("Make sure you've uploaded the code with baud rate 921600")
        print("\nAvailable ports:")
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        for port in ports:
            print(f"  - {port.device}")
        sys.exit(1)
    
    port = sys.argv[1]
    fps = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    
    viewer = FastVideoStreamViewer(port, baudrate=921600, target_fps=fps)
    viewer.start_stream()
