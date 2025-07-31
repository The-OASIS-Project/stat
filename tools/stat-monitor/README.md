# OASIS STAT Monitor

A real-time GUI application for monitoring OASIS STAT telemetry data via MQTT.

## Features

- **Real-time display** of battery, BMS, and system telemetry
- **Dynamic tabs** - Creates separate tabs for each battery source detected
- **Multiple battery source support** with dedicated tabs per source
- **Dark theme** optimized for operational environments  
- **Dynamic interface** adapts based on available data sources:
  - **Individual Source Tabs**: INA238 Power Monitor, Daly BMS, Unified Battery, etc.
  - **Cell-level data**: Voltage table with proper spacing (only for sources with cell data)
  - **Temperature sensors**: BMS thermal monitoring tables
  - **Fault monitoring**: Separate fault sections for each source
  - **System Tab**: CPU usage, memory usage, fan speed, system power channels
- **Color-coded status indicators** for charge states and battery levels
- **Connection monitoring** with automatic reconnection
- **Visual indicators** for battery level and system health
- **Debug mode** for troubleshooting multiple data sources

## Screenshots

The monitor displays data in dynamic tabs based on detected sources:

### Battery Tabs
- Pack voltage, current, power measurements
- Battery level with visual progress bar
- Time remaining estimation
- Temperature monitoring
- Battery chemistry information

### BMS Tab
- Individual cell voltages in sortable table
- Cell balancing status
- Charge/discharge FET states with color-coded charge states
- Active fault display with severity levels
- Pack health overview
- Temperature sensor monitoring

### System Tab
- CPU usage with progress bar
- Memory usage monitoring
- Fan speed and load percentage
- System power channels (INA3221) with voltage/current/power per channel
- Real-time system metrics

## Installation

1. **Install dependencies:**
   ```bash
   chmod +x setup.sh
   ./setup.sh
   ```

2. **Alternative manual installation:**
   ```bash
   # Install Python dependencies
   sudo apt-get install python3 python3-pip python3-tk
   pip3 install -r requirements.txt
   
   # Make executable
   chmod +x stat_monitor.py
   ```

## Usage

### Basic Usage
```bash
# Connect to local MQTT broker
./stat_monitor.py

# Connect to remote MQTT broker
./stat_monitor.py --host 192.168.1.100

# Use custom port and topic
./stat_monitor.py --host localhost --port 1883 --topic oasis/stat

# Enable debug mode to see tab creation and message routing
./stat_monitor.py --debug
```

### Command Line Options
```
--host HOST     MQTT broker hostname or IP (default: localhost)
--port PORT     MQTT broker port (default: 1883)
--topic TOPIC   MQTT topic to subscribe to (default: stat)
--debug         Enable debug output for MQTT messages
--help          Show help message
```

### Debug Mode
To troubleshoot multiple battery sources or message routing:
```bash
./stat_monitor.py --debug
```

This will show which MQTT messages are being received and how they're being routed to different data sources.

### Desktop Integration
To add to your desktop environment:

1. **Edit the desktop entry:**
   ```bash
   # Update the Exec path in oasis-stat-monitor.desktop
   sed -i 's|/path/to/stat_monitor.py|'$(pwd)'/stat_monitor.py|' oasis-stat-monitor.desktop
   ```

2. **Install desktop entry:**
   ```bash
   cp oasis-stat-monitor.desktop ~/.local/share/applications/
   ```

## Dynamic Battery Source Tabs

The monitor now creates **separate tabs for each battery source** instead of a single prioritized view:

### Dynamic Tab Creation
- **Tabs appear automatically** as different battery sources start publishing  
- **Each source gets its own tab**: "INA238 Power Monitor", "Daly BMS", "Unified Battery"
- **No more UI bouncing** - each source maintains its own stable display
- **Specialized layouts** - tabs adapt to show relevant data (cells, faults, etc.)

### Source-Specific Features
- **INA238 Power Monitor**: Precision voltage/current/power measurements
- **Daly BMS**: Complete BMS view with individual cell voltages, balancing status, FET states, faults
- **Unified Battery**: Combined view with best-available data from all sources
- **System Power (INA3221)**: Multi-channel power monitoring

### Improved Data Display
- **Fixed overlapping columns** with proper spacing and row heights
- **Sortable tables** with status color coding (red=critical, yellow=warning)
- **Color-coded charge states**: Green (charging), Orange (discharging), Gray (idle)
- **Balancing indicators** show active cell balancing
- **Temperature sensor tables** for BMS thermal monitoring
- **Comprehensive fault display** with severity categorization

## Configuration

The monitor automatically connects to your MQTT broker and subscribes to telemetry data. Make sure:

1. **OASIS STAT is running** and publishing data
2. **MQTT broker is accessible** (test with `mosquitto_sub -h localhost -t stat`)
3. **No firewall blocking** MQTT traffic (port 1883 by default)

## Data Sources

The monitor displays data from multiple STAT sources:

### Battery Data (INA238/Unified)
- Voltage, current, power measurements
- Battery level percentage
- Time remaining estimation
- Temperature readings
- Battery chemistry and capacity

### BMS Data (Daly BMS)
- Individual cell voltages
- Cell balancing status
- Charge/discharge FET states
- Temperature sensors
- Fault conditions
- Pack health analysis

### System Data
- CPU usage percentage
- Memory usage percentage
- Fan speed (RPM) and load
- System power channels (INA3221)

## Troubleshooting

### Connection Issues
- **"Disconnected" status**: Check MQTT broker is running
- **"No data for Xs"**: Verify STAT is publishing data
- **Connection failed**: Check host/port settings

### Missing Data
- **Empty tabs**: Check MQTT topic matches STAT configuration
- **No tabs appearing**: Ensure STAT is publishing battery data with correct device types
- **Missing cell data**: Only sources with cell-level data (like Daly BMS) show cell tables
- **No system data**: Check if system monitoring is enabled in STAT

### Dynamic Tab Issues
- **Tabs not appearing**: Check debug mode to see if MQTT messages are being received
- **Wrong tab names**: Verify device type and subtype in MQTT messages
- **Missing sections**: Cell voltages and faults only appear for sources that provide this data

### Display Issues
- **Fixed overlapping**: Columns now have proper spacing and row heights
- **Empty tables**: Source needs to publish relevant data arrays in MQTT
- **Missing colors**: Ensure data includes status fields for color coding

### Debug Mode
Run with debug flag to see detailed output:
```bash
./stat_monitor.py --host your-mqtt-host --debug
```

## Requirements

- **Python 3.6+** with tkinter support
- **paho-mqtt** library for MQTT communication
- **Linux desktop environment** (tested on Ubuntu, should work on most distributions)
- **OASIS STAT** running and publishing to MQTT

## Customization

The monitor can be easily customized:

- **Colors**: Edit the `setup_styles()` method
- **Layout**: Modify the tab creation methods
- **Data parsing**: Update `on_mqtt_message()` for custom data formats
- **Update rate**: Adjust the sleep time in `update_loop()`

## Integration with OASIS

This monitor is designed to work seamlessly with the OASIS ecosystem:

- **STAT**: Real-time telemetry collection and MQTT publishing

The monitor subscribes to the same MQTT topics used by other OASIS modules, providing a unified monitoring experience.

## License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
