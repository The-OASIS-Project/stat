# STAT - System Telemetry and Analytics Tracker

STAT is the OASIS subsystem responsible for monitoring internal hardware conditions and broadcasting live telemetry across the network. It tracks critical metrics such as power levels, CPU usage, memory load, and thermal status, then publishes this data via MQTT for consumption by other modules like DAWN (voice interface), MIRAGE (HUD), and other systems. 

Designed for extensibility, STAT serves as the diagnostic heartbeat of the suit—reporting and keeping the entire system informed and in sync.

## Features

- **Multi-source Power Monitoring**: Support for INA238 (single-channel) and INA3221 (multi-channel) power monitors
- **Auto-detection of Hardware**: Automatically detects and configures for available power monitors
- **Daly BMS Integration**: Full support for Daly Smart BMS with cell-level monitoring
- **Unified Battery Monitoring**: Combines data from multiple sources for comprehensive battery status
- **Real-time Hardware Monitoring**: Continuous monitoring of voltage, current, power, and temperature
- **Battery Time Estimation**: Advanced runtime prediction based on battery chemistry, capacity, and load
- **Battery Health Diagnostics**: Cell-level voltage analysis with deviation detection
- **Accurate Battery Status**: Non-linear state of charge calculation using chemistry-specific discharge curves
- **Temperature Compensation**: Adjusts battery capacity estimates based on temperature conditions
- **ARK Electronics Jetson Carrier Support**: Automatic detection with optimized settings
- **OASIS Integration Ready**: Designed for integration with DAWN, MIRAGE, and other modules
- **MQTT Telemetry Broadcasting**: Publishes structured data for network consumption
- **Professional Telemetry Display**: Clean, organized output with status indicators
- **System Monitoring**: CPU usage, memory usage, and fan speed tracking
- **Service Mode**: Can run as a background service with systemd integration

## Building

### Prerequisites

- Linux system with I2C support
- GCC compiler
- CMake 3.10 or later
- Make build system
- Mosquitto MQTT client libraries
- json-c library

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
# Run with auto-detection (will detect ARK board and power monitors)
./oasis-stat

# Force specific power monitor type
./oasis-stat --monitor ina238
./oasis-stat --monitor ina3221
./oasis-stat --monitor both

# Custom configuration
./oasis-stat --bus /dev/i2c-7 --shunt 0.001 --current 10.0

# Enable Daly BMS monitoring
./oasis-stat --bms-enable
```

### Command Line Options

| Option | Long Form | Description | Default |
|--------|-----------|-------------|---------|
| `-b` | `--bus` | I2C bus device path | `/dev/i2c-1` (or `/dev/i2c-7` for ARK) |
| `-a` | `--address` | I2C device address | `0x45` |
| `-s` | `--shunt` | Shunt resistor value (Ω) | `0.0003` (or `0.001` for ARK) |
| `-c` | `--current` | Maximum current (A) | `327.68` (or `10.0` for ARK) |
| `-i` | `--interval` | Sampling interval (ms) | `1000` |
| `-m` | `--monitor` | Power monitor type: ina238, ina3221, both, auto | `auto` |
| | `--battery` | Battery type | `5S_Li-ion` |
| | `--battery-min` | Custom battery minimum voltage | Type-specific |
| | `--battery-max` | Custom battery maximum voltage | Type-specific |
| | `--battery-warn` | Battery warning threshold percent | `20` |
| | `--battery-crit` | Battery critical threshold percent | `10` |
| | `--battery-capacity` | Battery capacity in mAh | Type-specific |
| | `--battery-chemistry` | Battery chemistry (Li-ion, LiPo, LiFePO4, NiMH, Lead-Acid) | Type-specific |
| | `--battery-cells` | Number of cells in series | Type-specific |
| | `--battery-parallel` | Number of cells in parallel | `1` |
| | `--bms-enable` | Enable Daly BMS monitoring | Disabled |
| | `--bms-port` | Serial port for BMS | `/dev/ttyTHS1` |
| | `--bms-baud` | BMS baud rate | `9600` |
| | `--bms-interval` | BMS polling interval (ms) | `1000` |
| | `--bms-set-capacity` | Set BMS rated capacity (mAh) | - |
| | `--bms-set-soc` | Set BMS state of charge (%) | - |
| | `--bms-warn-thresh` | Cell voltage warning threshold (mV) | `70` |
| | `--bms-crit-thresh` | Cell voltage critical threshold (mV) | `120` |
| `-H` | `--mqtt-host` | MQTT broker hostname | `localhost` |
| `-P` | `--mqtt-port` | MQTT broker port | `1883` |
| `-T` | `--mqtt-topic` | MQTT topic to publish to | `stat` |
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
| `3S_1500mAh_LiPo` | 3S 1500mAh LiPo battery | 3S1P | 1500 mAh | LiPo |

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

# Enable Daly BMS with cell monitoring
./oasis-stat --bms-enable --bms-port /dev/ttyTHS1

# Force specific power monitor type
./oasis-stat --monitor ina3221

# Override auto-detected settings
./oasis-stat --shunt 0.0005 --current 15.0

# Use completely custom settings
./oasis-stat --bus /dev/i2c-1 --address 0x44 --shunt 0.0003 --current 327.68
```

## Power Monitoring Options

STAT supports multiple power monitoring methods:

### Auto-detection (Default)

By default, STAT automatically detects available power monitors in this order:
1. Checks for INA3221 via sysfs interface
2. Checks for INA238 via direct I2C access
3. Uses the best available monitor(s)

### INA238 Single-Channel Power Monitor

The INA238 provides high-precision voltage, current, and power measurements via I2C:
- High accuracy (0.1% typical)
- Wide common-mode voltage range
- Internal temperature sensor
- Ideal for single-source monitoring

### INA3221 Multi-Channel Power Monitor

The INA3221 provides three-channel monitoring:
- Simultaneous monitoring of multiple power rails
- Each channel reports voltage, current, and power
- Ideal for systems with multiple power sources
- Accessed via Linux hwmon interface

### Unified Monitoring

When multiple sources are available, STAT can combine data for a unified view:
- Prioritizes the most accurate source for each metric
- Combines INA238 precision with BMS cell-level data
- Provides comprehensive health monitoring

## Battery Monitoring Features

### Daly Smart BMS Integration

STAT provides comprehensive integration with Daly Smart BMS:

- **Auto-detection**: Automatically finds BMS on common serial ports
- **Cell-level Monitoring**: Individual cell voltage tracking
- **Balance Status**: Cell balancing monitoring
- **FET Status**: Charge and discharge MOSFET state
- **Temperature Sensors**: Multiple temperature sensor support
- **Fault Reporting**: Detailed fault detection and categorization
- **Health Analysis**: Cell deviation detection with severity levels
- **Configuration**: Set capacity and SOC via command line

### Battery Health Diagnostics

The battery health monitoring system:

1. Calculates cell voltage statistics (min, max, average, delta)
2. Detects deviations from average with configurable thresholds
3. Categorizes cells as NORMAL, WARNING, or CRITICAL
4. Provides detailed health status with reasons
5. Monitors cell balancing activity
6. Categorizes BMS faults by severity
7. Publishes comprehensive health data via MQTT

### Battery Time Estimation

STAT uses an advanced battery time estimation algorithm that takes into account:

1. **Battery Chemistry**: Different discharge curves for Li-ion, LiPo, LiFePO4, etc.
2. **Temperature Effects**: Reduced capacity at lower temperatures
3. **Current Load**: Actual measured current draw for accurate predictions
4. **Cell Configuration**: Number of cells in series and parallel
5. **Adaptive Smoothing**: Prevents estimates from jumping erratically

The estimation process:
1. Calculates current state of charge using chemistry-specific discharge curves
2. Applies temperature compensation to the battery capacity
3. Determines remaining capacity based on the state of charge
4. Calculates runtime by dividing remaining capacity by current draw
5. Applies adaptive smoothing based on current stability

For optimal accuracy:
- Use the correct battery chemistry and capacity
- Ensure the temperature sensor is positioned to reflect the battery temperature
- Allow the system to run for a few minutes to stabilize readings

## ARK Electronics Jetson Carrier

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

## MQTT Integration

STAT publishes telemetry data to MQTT topics for consumption by other systems:

### Published Data Types

- **Battery Data (INA238)**: Voltage, current, power, temperature, SOC, time remaining
- **BMS Data (Daly)**: Cell voltages, temperatures, FET status, fault conditions
- **Battery Health**: Cell statistics, deviation analysis, fault categorization
- **System Power (INA3221)**: Multi-channel power measurements
- **System Metrics**: CPU usage, memory usage, fan speed
- **Unified Battery**: Combined data from all sources with prioritization

### Data Format

All data is published in JSON format with device type identifiers:

```json
{
  "device": "Battery",
  "type": "INA238",
  "voltage": 15.842,
  "current": 1.512,
  "power": 23.953,
  "temperature": 46.38,
  "battery_level": 76.0,
  "time_remaining_min": 303.5,
  "time_remaining_fmt": "5:03",
  "battery_status": "NORMAL"
}
```

### STAT Monitor GUI

For visual monitoring, STAT provides a Python-based GUI that:
1. Connects to the MQTT broker
2. Displays real-time telemetry in a user-friendly interface
3. Shows battery levels, cell voltages, system metrics, and more
4. Auto-detects and displays multiple data sources

Run the monitor with:
```bash
cd tools/stat-monitor
./stat_monitor.py
```

## Architecture

### STAT Design Principles

1. **Telemetry Focus**: Designed for data collection and broadcasting, not control
2. **Multi-source Integration**: Combines data from multiple sensors for comprehensive monitoring
3. **Extensible Architecture**: Modular design supports additional sensors and platforms
4. **Network Ready**: Structured for easy integration with MQTT and other protocols
5. **Professional Display**: Clean, organized output suitable for operational environments
6. **Robust Operation**: Comprehensive error handling and graceful degradation

### OASIS Integration

STAT is designed with hooks for integration with other OASIS components:

- **MQTT Publishing**: Telemetry data ready for network broadcast
- **JSON Output**: Structured data format for API consumption
- **Alert Thresholds**: Configurable limits for automated notifications
- **Historical Logging**: Data persistence for trend analysis
- **Multi-sensor Support**: Framework for additional hardware monitoring

## Troubleshooting

### Permission Issues

```bash
# Add user to i2c group
sudo usermod -a -G i2c $USER
# Add user to dialout for serial port access
sudo usermod -a -G dialout $USER
# Log out and log back in for changes to take effect
```

### Build Issues

```bash
# Install required packages (Ubuntu/Debian)
sudo apt-get install build-essential cmake libmosquitto-dev libjson-c-dev

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

# Check serial port for BMS
ls -l /dev/ttyTHS*
ls -l /dev/ttyUSB*
```

### STAT Troubleshooting

- **No ARK Detection**: Verify `/dev/i2c-7` exists and EEPROM is accessible
- **INA238 Communication**: Check wiring, power supply, and I2C address
- **INA3221 Detection**: Verify sysfs interface available at `/sys/bus/i2c/drivers/ina3221`
- **BMS Connection**: Check serial port permissions and baud rate settings
- **Telemetry Errors**: Validate shunt resistor value and current range settings
- **Display Issues**: Ensure terminal supports ANSI escape sequences
- **Battery Time Estimate Errors**: Verify battery configuration matches physical battery

## License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.  If not, see <https://www.gnu.org/licenses/>.

By contributing to this project, you agree to license your contributions under the GPLv3 (or any later version) or any future licenses chosen by the project author(s). Contributions include any modifications, enhancements, or additions to the project. These contributions become part of the project and are adopted by the project author(s).
