#!/bin/bash
# Quick script to receive photos from CyberGlass camera

echo "🚀 Starting CyberGlass Photo Receiver..."
echo ""

# Check if Python is installed
if ! command -v python3 &> /dev/null; then
    echo "❌ Python3 is not installed!"
    exit 1
fi

# Create virtual environment if it doesn't exist
if [ ! -d "venv" ]; then
    echo "📦 Creating Python virtual environment..."
    python3 -m venv venv
fi

# Activate virtual environment
echo "🔧 Activating virtual environment..."
source venv/bin/activate

# Check if pyserial is installed in venv
if ! python -c "import serial" 2>/dev/null; then
    echo "📦 Installing pyserial in virtual environment..."
    pip install pyserial
fi

# Create photos directory if it doesn't exist
mkdir -p photos

# List available ports
echo "📋 Available serial ports:"
python tools/receive_photo.py --list
echo ""

# Check if port is provided
if [ -z "$1" ]; then
    echo "Usage: ./receive.sh <serial_port>"
    echo ""
    echo "Examples:"
    echo "  ./receive.sh /dev/cu.usbmodem14201"
    echo "  ./receive.sh /dev/ttyACM0"
    echo ""
    deactivate
    exit 1
fi

# Start receiving
echo "🎥 Starting photo receiver on $1..."
echo "📁 Photos will be saved to: ./photos/"
echo ""
echo "💡 On the device, send command 'send' or 'd' to capture and transmit a photo"
echo "Press Ctrl+C to exit"
echo ""

python tools/receive_photo.py --port "$1" --output ./photos

# Deactivate virtual environment on exit
deactivate
