#!/bin/bash
# Convenience script for starting video stream

# Activate virtual environment if it exists
if [ -d "venv" ]; then
    source venv/bin/activate
fi

# Default values
PORT="${1:-/dev/cu.usbmodem14201}"
FPS="${2:-10}"

# Check if port exists
if [ ! -e "$PORT" ]; then
    echo "Error: Port $PORT does not exist"
    echo "Available ports:"
    ls /dev/cu.* 2>/dev/null || echo "No ports found"
    exit 1
fi

echo "Starting video stream..."
echo "Port: $PORT"
echo "Target FPS: $FPS"
echo ""

python3 tools/video_stream.py "$PORT" "$FPS"
