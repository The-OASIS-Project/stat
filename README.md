# STAT - System Telemetry and Analytics Tracker

STAT is the OASIS subsystem responsible for monitoring internal hardware conditions and broadcasting live telemetry across the network. It tracks critical metrics such as power levels, CPU usage, memory load, and thermal status, then publishes this data via MQTT for consumption by other modules like DAWN (voice interface) and MIRAGE (HUD). 

Designed for extensibility, STAT serves as the diagnostic heartbeat of the suit—reporting and keeping the entire system informed and in sync.

## Features

- **Real-time Hardware Monitoring**: Continuous monitoring of voltage, current, power, and temperature
- **Battery Time Estimation**: Advanced runtime prediction based on battery chemistry, capacity, and load
- **Accurate Battery Status**: Non-linear state of charge calculation using chemistry-specific discharge curves
- **Temperature Compensation**: Adjusts battery capacity estimates based on temperature conditions
- **ARK Electronics Jetson Carrier Support**: Automatic detection with optimized settings
- **OASIS Integration Ready**: Designed for integration with DAWN, MIRAGE, and other modules
- **Professional Telemetry Display**: Clean, organized output with status indicators
- **Modular Design**: Clean separation of concerns with dedicated modules
- **Robust I2C Communication**: Comprehensive I2C utilities with error handling
- **Command-line Interface**: Flexible configuration options

## Building

### Prerequisites

- Linux system with I2C support
- GCC compiler
- CMake 3.10 or later
- Make build system

### CMake Build

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
make -j$(nproc)

# Installation (basic)
sudo make install

# For complete installation with service setup
cd ..
sudo ./install.sh
```

## Usage

### Basic Usage

```bash
# Run with auto-detection (will detect ARK board if present)
./oasis-stat

# Custom configuration
./oasis-stat --bus /dev/i2c-7 --shunt 0.001 --current 10.0
```

### Command Line Options

| Option | Long Form | Description | Default |
|--------|-----------|-------------|---------|
| `-b` | `--bus` | I2C bus device path | `/dev/i2c-1` (or `/dev/i2c-7` for ARK) |
| `-a` | `--address` | I2C device address | `0x45` |
| `-s` | `--shunt` | Shunt resistor value (Ω) | `0.0003` (or `0.001` for ARK) |
| `-c` | `--current` | Maximum current (A) | `327.68` (or `10.0` for ARK) |
| `-i` | `--interval` | Sampling interval (ms) | `1000` |
| | `--battery` | Battery type | `5S_Li-ion` |
| | `--battery-min` | Custom battery minimum voltage | Type-specific |
| | `--battery-max` | Custom battery maximum voltage | Type-specific |
| | `--battery-warn` | Battery warning threshold percent | `20` |
| | `--battery-crit` | Battery critical threshold percent | `10` |
| | `--battery-capacity` | Battery capacity in mAh | Type-specific |
| | `--battery-chemistry` | Battery chemistry (Li-ion, LiPo, LiFePO4, NiMH, Lead-Acid) | Type-specific |
| | `--battery-cells` | Number of cells in series | Type-specific |
| | `--battery-parallel` | Number of cells in parallel | `1` |
| | `--list-batteries` | Show available battery configurations | - |
| `-e` | `--service` | Run in service mode | - |
| `-h` | `--help` | Show help message | - |
| `-v` | `--version` | Show version information | - |

### Predefined Battery Configurations

STAT includes several predefined battery configurations:

| Name | Description | Cells | Capacity | Chemistry |
|------|-------------|-------|----------|-----------|
| `4S_Li-ion` | Standard 4S Li-ion battery | 4S1P | 2600 mAh | Li-ion |
| `5S_Li-ion` | Standard 5S Li-ion battery | 5S1P | 2600 mAh | Li-ion |
| `6S_Li-ion` | Standard 6S Li-ion battery | 6S1P | 2600 mAh | Li-ion |
| `2S_LiPo` | Standard 2S LiPo battery | 2S1P | 5000 mAh | LiPo |
| `3S_LiPo` | Standard 3S LiPo battery | 3S1P | 5000 mAh | LiPo |
| `6S_LiPo` | Standard 6S LiPo battery | 6S1P | 5000 mAh | LiPo |
| `4S2P_Samsung50E` | Samsung 50E 21700 battery | 4S2P | 10000 mAh | Li-ion |
| `3S_5200mAh_LiPo` | 3S 5200mAh LiPo battery | 3S1P | 5200 mAh | LiPo |
| `3S_2200mAh_LiPo` | 3S 2200mAh LiPo battery | 3S1P | 2200 mAh | LiPo |

### Example Commands

```bash
# Display version and OASIS integration info
./oasis-stat --version

# Run with custom sampling rate
./oasis-stat --interval 500

# Run with specific battery configuration
./oasis-stat --battery 4S2P_Samsung50E

# Use custom battery settings
./oasis-stat --battery custom --battery-min 12.0 --battery-max 16.8 --battery-capacity 10000 --battery-chemistry Li-ion --battery-cells 4 --battery-parallel 2

# Override auto-detected settings
./oasis-stat --shunt 0.0005 --current 15.0

# Use completely custom settings
./oasis-stat --bus /dev/i2c-1 --address 0x44 --shunt 0.0003 --current 327.68
```

### ARK Electronics Jetson Carrier

When running on an ARK Electronics Jetson Carrier, the application automatically:

1. **Detects the board** by reading the serial number from the AT24CSW010 EEPROM
2. **Displays the serial number** during startup and in the monitoring interface
3. **Applies optimized defaults**:
   - I2C Bus: `/dev/i2c-7`
   - Shunt Resistor: `0.001 Ω` (1mΩ)
   - Maximum Current: `10.0 A`

Example output:
```
═══════════════════════════════════════════════════════════════
  STAT - System Telemetry and Analytics Tracker v1.0.0
  OASIS Hardware Monitoring and Telemetry Collection
═══════════════════════════════════════════════════════════════
Platform: ARK Jetson Carrier (S/N: 00000000000000000000000000000000)
Battery: 4S2P_Samsung50E (14.4V - 16.8V)
Status: ONLINE - Telemetry collection active
Press Ctrl+C to shutdown STAT

┌─────────────────────────────────────────────────────────────┐
│                    POWER TELEMETRY DATA                     │
├─────────────────────────────────────────────────────────────┤
│ Bus Voltage:      15.842 V                                  │
│ Current:           1.512 A                                  │
│ Power:            23.953 W                                  │
│ Temperature:       46.38 °C (INA238 die)                    │
│                                                             │
│ Battery Level:      76.0 %                                  │
│ Time Remaining:      5:03 h:m                               │
│ Battery:          Li-ion (4 cells, 10000 mAh)               │
│ Battery Status:   NORMAL                                    │
└─────────────────────────────────────────────────────────────┘

[STAT] Telemetry broadcast ready for OASIS network consumption
```

## Battery Time Estimation

STAT uses an advanced battery time estimation algorithm that takes into account:

1. **Battery Chemistry**: Different discharge curves for Li-ion, LiPo, LiFePO4, etc.
2. **Temperature Effects**: Reduced capacity at lower temperatures
3. **Current Load**: Actual measured current draw for accurate predictions
4. **Cell Configuration**: Number of cells in series and parallel

The estimation process:
1. Calculates current state of charge using chemistry-specific discharge curves
2. Applies temperature compensation to the battery capacity
3. Determines remaining capacity based on the state of charge
4. Calculates runtime by dividing remaining capacity by current draw

For optimal accuracy:
- Use the correct battery chemistry and capacity
- Ensure the temperature sensor is positioned to reflect the battery temperature
- Allow the system to run for a few minutes to stabilize readings

## Architecture

### STAT Design Principles

1. **Telemetry Focus**: Designed for data collection and broadcasting, not control
2. **Network Ready**: Structured for easy integration with MQTT and other protocols
3. **Extensible Architecture**: Modular design supports additional sensors and platforms
4. **Professional Display**: Clean, organized output suitable for operational environments
5. **Robust Operation**: Comprehensive error handling and graceful degradation

### Future OASIS Integration

STAT is designed with hooks for future integration:

- **MQTT Publishing**: Telemetry data ready for network broadcast
- **JSON Output**: Structured data format for API consumption
- **Alert Thresholds**: Configurable limits for automated notifications
- **Historical Logging**: Data persistence for trend analysis
- **Multi-sensor Support**: Framework for additional hardware monitoring

## Development

### Build Configuration

```bash
# Debug build with symbols
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Release build optimized
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

### Adding New Hardware Support

1. **New Sensor Modules**: Create modules following the `ina238.c` pattern
2. **Platform Detection**: Extend `ark_detection.c` or create new detection modules
3. **I2C Devices**: Utilize `i2c_utils.c` for consistent communication
4. **Telemetry Integration**: Add new measurements to the display framework

### Code Organization

- **Clean Interfaces**: Well-defined APIs between modules
- **Error Handling**: Consistent error reporting and recovery
- **Resource Management**: Proper initialization and cleanup
- **Documentation**: Comprehensive inline and API documentation

## Troubleshooting

### Permission Issues

```bash
# Add user to i2c group
sudo usermod -a -G i2c $USER
# Log out and log back in for changes to take effect
```

### Build Issues

```bash
# Install required packages (Ubuntu/Debian)
sudo apt-get install build-essential cmake

# Clean rebuild
rm -rf build && mkdir build && cd build
cmake .. && make
```

### Hardware Detection

```bash
# Verify I2C bus availability
ls /dev/i2c-*

# Scan for I2C devices
i2cdetect -y 7  # For ARK boards
i2cdetect -y 1  # For generic systems

# Check device response
i2cget -y 7 0x45 0x3e w  # Read manufacturer ID from INA238
```

### STAT Troubleshooting

- **No ARK Detection**: Verify `/dev/i2c-7` exists and EEPROM is accessible
- **INA238 Communication**: Check wiring, power supply, and I2C address
- **Telemetry Errors**: Validate shunt resistor value and current range settings
- **Display Issues**: Ensure terminal supports ANSI escape sequences
- **Battery Time Estimate Errors**: Verify battery configuration matches physical battery

## Security Considerations

### Access Control
- **I2C Permissions**: Requires i2c group membership
- **File System**: Read-only access to device files
- **Network**: MQTT server running with appropriate permissions

### Data Privacy
- **Local Processing**: All telemetry processing local to device
- **No External Dependencies**: Self-contained operation
- **Configurable Publishing**: User control over data transmission

## License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.  If not, see <https://www.gnu.org/licenses/>.

By contributing to this project, you agree to license your contributions under the GPLv3 (or any later version) or any future licenses chosen by the project author(s). Contributions include any modifications, enhancements, or additions to the project. These contributions become part of the project and are adopted by the project author(s).

## Support

For issues related to STAT or OASIS integration:

1. Check the troubleshooting section above
2. Verify hardware connections and permissions
3. Review system logs for I2C communication errors
4. Test with known-good hardware configurations

The modular structure makes it easy to:
- Add support for additional power monitors and sensors
- Create drivers for other hardware platforms
- Implement MQTT publishing and network telemetry
- Add data logging and historical analysis
- Integrate with voice, HUD, and orchestration modules
- Reuse components across the OASIS project
