#!/bin/bash
# Setup script for OASIS STAT Monitor

set -e

echo "Setting up OASIS STAT Monitor..."

# Check if Python3 is installed
if ! command -v python3 &> /dev/null; then
   echo "Error: Python3 is not installed. Please install Python3 first."
   echo "On Ubuntu/Debian: sudo apt-get install python3 python3-pip python3-tk"
   exit 1
fi

# Check if tkinter is available
if ! python3 -c "import tkinter" &> /dev/null; then
   echo "Error: tkinter is not available. Please install it:"
   echo "On Ubuntu/Debian: sudo apt-get install python3-tk"
   exit 1
fi

# Check if pip is installed
if ! command -v pip3 &> /dev/null; then
   echo "Error: pip3 is not available. Please install it:"
   echo "On Ubuntu/Debian: sudo apt-get install python3-pip"
   exit 1
fi

# Install Python requirements
echo "Installing Python dependencies..."
pip3 install -r requirements.txt

# Make the monitor script executable
chmod +x stat_monitor.py

echo ""
echo "Setup complete!"
echo ""
echo "Usage:"
echo "  ./stat_monitor.py                    # Connect to localhost:1883"
echo "  ./stat_monitor.py --host 192.168.1.100  # Connect to remote broker"
echo "  ./stat_monitor.py --help             # Show all options"
echo ""
echo "Make sure your OASIS STAT is running and publishing to MQTT."
