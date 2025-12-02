# Quick Start - CyberGlass One-Click Connect

## One-Minute Quick Start

### 1. Setup Virtual Environment and Install Dependencies (First Time Only)

```bash
# Create virtual environment (if not already created)
python3 -m venv venv

# Activate virtual environment
source venv/bin/activate

# Install dependencies
pip install -r tools/requirements.txt
```

> **Tip**: After activation, you'll see `(venv)` in your command prompt

### 2. Run the Script

```bash
# Ensure virtual environment is activated (you should see (venv) in prompt)
cd tools
python ble_video_stream_test.py
```