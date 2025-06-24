#!/bin/bash
# Installation script for OASIS STAT

set -e

# Get the directory where the script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Build and install the binary
echo "Building OASIS STAT..."
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

echo "Installing OASIS STAT binary..."
sudo make install

# Create oasis user if it doesn't exist
if ! id -u oasis &>/dev/null; then
    echo "Creating oasis user..."
    sudo useradd -r -s /bin/false oasis
fi

# Set up permissions for I2C access
echo "Setting up I2C permissions..."
sudo usermod -a -G i2c oasis

# Create config directory
echo "Setting up configuration directory..."
sudo mkdir -p /etc/oasis

# Install configuration file
echo "Installing configuration file..."
sudo cp "${SCRIPT_DIR}/config/stat.conf" /etc/oasis/
sudo chown oasis:oasis /etc/oasis/stat.conf
sudo chmod 640 /etc/oasis/stat.conf

# Install systemd service file
echo "Installing systemd service..."
sudo cp "${SCRIPT_DIR}/config/oasis-stat.service" /etc/systemd/system/

# Reload systemd and enable service
sudo systemctl daemon-reload
sudo systemctl enable oasis-stat.service

echo "Installation complete!"
echo "To start the service, run: sudo systemctl start oasis-stat"
echo "To check the status, run: sudo systemctl status oasis-stat"
echo "To view logs, run: sudo journalctl -u oasis-stat"
echo "Configuration file: /etc/oasis/stat.conf"

