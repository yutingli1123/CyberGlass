#!/bin/bash
# Fast binary video stream viewer

# Activate virtual environment if it exists
if [ -d "venv" ]; then
    source venv/bin/activate
fi

# Default values
PORT="${1:-/dev/cu.usbmodem14201}"
FPS="${2:-30}"

# Check if port exists
if [ ! -e "$PORT" ]; then
    echo "Error: Port $PORT does not exist"
    echo "Available ports:"
    ls /dev/cu.* 2>/dev/null || echo "No ports found"
    exit 1
fi

echo "╔════════════════════════════════════════════╗"
echo "║  Fast Binary Video Stream (921600 baud)   ║"
echo "╚════════════════════════════════════════════╝"
echo ""

python3 tools/fast_video_stream.py "$PORT" "$FPS"
