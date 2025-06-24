/**
 * @file oasis-stat.c
 * @brief STAT - System Telemetry and Analytics Tracker for OASIS
 * 
 * STAT is the OASIS subsystem responsible for monitoring internal hardware 
 * conditions and broadcasting live telemetry across the network. It tracks 
 * critical metrics such as power levels, CPU usage, memory load, and thermal 
 * status, then publishes this data via MQTT for consumption by other modules 
 * like DAWN (voice interface), MIRAGE (HUD), and other systems. 
 * 
 * Designed for extensibility, STAT serves as the diagnostic heartbeat of the 
 * suit—reporting and keeping the entire system informed and in sync.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * By contributing to this project, you agree to license your contributions
 * under the GPLv3 (or any later version) or any future licenses chosen by
 * the project author(s). Contributions include any modifications,
 * enhancements, or additions to the project. These contributions become
 * part of the project and are adopted by the project author(s).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>

#include "ark_detection.h"
#include "cpu_monitor.h"
#include "fan_monitor.h"
#include "i2c_utils.h"
#include "ina238.h"
#include "logging.h"
#include "memory_monitor.h"
#include "mqtt_publisher.h"

/* Application Configuration */
#define DEFAULT_SAMPLING_INTERVAL_MS    1000
#define MIN_SAMPLING_INTERVAL_MS        100
#define MAX_SAMPLING_INTERVAL_MS        10000

/* Predefined battery configurations */
static const battery_config_t battery_configs[] = {
    {13.2f, 16.8f, 20.0f, 10.0f, "4S_Li-ion"},     // Nominal 14.4 V (3.6 V/cell)
    {16.5f, 21.0f, 20.0f, 10.0f, "5S_Li-ion"},     // Nominal 18.0 V (3.6 V/cell)
    {19.8f, 25.2f, 20.0f, 10.0f, "6S_Li-ion"},     // Nominal 21.6 V (3.6 V/cell)

    {6.6f,  8.4f,  20.0f, 10.0f, "2S_LiPo"},       // Nominal 7.4  V (3.7 V/cell)
    {9.9f, 12.6f,  20.0f, 10.0f, "3S_LiPo"},       // Nominal 11.1 V (3.7 V/cell)
    {19.8, 25.2f,  20.0f, 10.0f, "6S_LiPo"},       // Nominal 22.2 V (3.7 V/cell)
    {0.0f,  0.0f,  0.0f,  0.0f,  "custom"}         // Custom configuration
};

#define NUM_BATTERY_CONFIGS ((int)(sizeof(battery_configs) / sizeof(battery_configs[0])))

/* STAT Version Information */
#define STAT_VERSION_MAJOR 1
#define STAT_VERSION_MINOR 0
#define STAT_VERSION_PATCH 0

/* New structures to hold system metrics */
typedef struct {
    float cpu_usage;
    float memory_usage;
    int fan_rpm;
    int fan_load;
    bool fan_available;
} system_metrics_t;

/* Global Variables */
static volatile bool g_running = true;

/* Function Prototypes */
static void print_usage(const char *prog_name);
static void print_version(void);
static void print_battery_configs(void);
static void signal_handler(int signal);
static void print_header(const ark_board_info_t *ark_info, const battery_config_t *battery);
static void print_measurements(const ina238_measurements_t *measurements,
                              const ark_board_info_t *ark_info,
                              const battery_config_t *battery,
                              const system_metrics_t *sys_metrics);
static float calculate_battery_percentage(float voltage, const battery_config_t *battery);
static const char* get_battery_status(float percentage, const battery_config_t *battery);

/**
 * @brief Signal handler for graceful shutdown
 */
static void signal_handler(int signal)
{
    (void)signal;  // Suppress unused parameter warning
    g_running = false;
}

/**
 * @brief Print STAT version information
 */
static void print_version(void)
{
    printf("STAT (System Telemetry and Analytics Tracker) v%d.%d.%d\n", 
           STAT_VERSION_MAJOR, STAT_VERSION_MINOR, STAT_VERSION_PATCH);
    printf("Part of the OASIS (Operator Assistance and Situational Intelligence System)\n");
    printf("Built on %s at %s\n", __DATE__, __TIME__);
    printf("\nSTAT is responsible for monitoring internal hardware conditions\n");
    printf("and broadcasting live telemetry across the OASIS network.\n");
}

/**
 * @brief Print available battery configurations
 */
static void print_battery_configs(void)
{
    printf("Available battery configurations:\n");
    for (int i = 0; i < NUM_BATTERY_CONFIGS - 1; i++) {  // Exclude 'custom'
        printf("  %-12s: %.1fV - %.1fV\n", 
               battery_configs[i].name,
               battery_configs[i].min_voltage,
               battery_configs[i].max_voltage);
    }
    printf("  %-12s: Use --battery-min and --battery-max to specify range\n", "custom");
}

/**
 * @brief Print application usage information
 */
static void print_usage(const char *prog_name)
{
    printf("Usage: %s [options]\n", prog_name);
    printf("\nSTAT - System Telemetry and Analytics Tracker\n");
    printf("Hardware monitoring and telemetry collection for OASIS\n");
    printf("\nOptions:\n");
    printf("  -b, --bus BUS          I2C bus device (default: /dev/i2c-1, or /dev/i2c-7 for ARK)\n");
    printf("  -a, --address ADDR     I2C device address (default: 0x45)\n");
    printf("  -s, --shunt SHUNT      Shunt resistor value in ohms (default: 0.0003, or 0.001 for ARK)\n");
    printf("  -c, --current MAX      Maximum current in amps (default: 327.68, or 10.0 for ARK)\n");
    printf("  -i, --interval MS      Sampling interval in milliseconds (default: 1000, range: 100-10000)\n");
    printf("      --battery TYPE     Battery type (default: 5S_Li-ion)\n");
    printf("      --battery-min V    Custom battery minimum voltage\n");
    printf("      --battery-max V    Custom battery maximum voltage\n");
    printf("      --battery-warn %%   Battery warning threshold percent (default: 20)\n");
    printf("      --battery-crit %%   Battery critical threshold percent (default: 10)\n");
    printf("      --list-batteries   Show available battery configurations\n");
    printf("  -e, --service          Run in service mode (use with systemd)\n");
    printf("  -h, --help             Show this help message\n");
    printf("  -v, --version          Show version information\n");
    printf("MQTT Options:\n");
    printf("  -H, --mqtt-host HOST   MQTT broker hostname (default: %s)\n", MQTT_DEFAULT_HOST);
    printf("  -P, --mqtt-port PORT   MQTT broker port (default: %d)\n", MQTT_DEFAULT_PORT);
    printf("  -T, --mqtt-topic TOPIC MQTT topic to publish to (default: %s)\n", MQTT_DEFAULT_TOPIC);
    printf("\nNote: If ARK Electronics Jetson Carrier is detected, optimized defaults are used.\n");
    printf("      Command-line options will override auto-detected settings.\n");
    printf("\nSTAT integrates with other OASIS modules:\n");
    printf("  • DAWN  - Voice interface and user interaction\n");
    printf("  • MIRAGE - Heads-up display and visual feedback\n");
}

/**
 * @brief Print application header with board information
 */
static void print_header(const ark_board_info_t *ark_info, const battery_config_t *battery)
{
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  STAT - System Telemetry and Analytics Tracker v%d.%d.%d\n", 
           STAT_VERSION_MAJOR, STAT_VERSION_MINOR, STAT_VERSION_PATCH);
    printf("  OASIS Hardware Monitoring and Telemetry Collection\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    if (ark_info->detected) {
        printf("Platform: ARK Jetson Carrier (S/N: %s)\n", ark_info->serial_number);
    } else {
        printf("Platform: Generic Linux System\n");
    }
    printf("Battery: %s (%.1fV - %.1fV)\n", battery->name, battery->min_voltage, battery->max_voltage);
    printf("Status: ONLINE - Telemetry collection active\n");
    printf("Press Ctrl+C to shutdown STAT\n\n");
}

/**
 * @brief Calculate battery percentage based on voltage
 */
static float calculate_battery_percentage(float voltage, const battery_config_t *battery)
{
    if (battery->max_voltage <= battery->min_voltage) {
        return 0.0f;  // Invalid battery configuration
    }
    
    float percentage = ((voltage - battery->min_voltage) / 
                       (battery->max_voltage - battery->min_voltage)) * 100.0f;
    
    // Clamp to 0-100%
    if (percentage < 0.0f) percentage = 0.0f;
    if (percentage > 100.0f) percentage = 100.0f;
    
    return percentage;
}

/**
 * @brief Get battery status string based on percentage
 */
static const char* get_battery_status(float percentage, const battery_config_t *battery)
{
    if (percentage <= battery->critical_percent) {
        return "CRITICAL";
    } else if (percentage <= battery->warning_percent) {
        return "WARNING";
    } else {
        return "NORMAL";
    }
}

/**
 * @brief Print current measurements to screen
 */
static void print_measurements(const ina238_measurements_t *measurements,
                              const ark_board_info_t *ark_info,
                              const battery_config_t *battery,
                              const system_metrics_t *sys_metrics)
{
    printf("\033[2J\033[H");  // Clear screen and move cursor to top

    /* Print header */
    print_header(ark_info, battery);

    /* Print telemetry data */
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│                  SYSTEM TELEMETRY DATA                      │\n");
    printf("├─────────────────────────────────────────────────────────────┤\n");

    /* Power section */
    printf("│ POWER                                                       │\n");

    if (measurements->valid) {
        printf("│ Bus Voltage:      %8.3f V                                │\n", measurements->bus_voltage);
        printf("│ Current:          %8.3f A                                │\n", measurements->current);
        printf("│ Power:            %8.3f W                                │\n", measurements->power);
        printf("│ Temperature:      %8.2f °C (INA238 die)                  │\n", measurements->temperature);

        /* Battery status */
        float battery_percent = calculate_battery_percentage(measurements->bus_voltage, battery);
        const char *battery_status = get_battery_status(battery_percent, battery);

        printf("│ Battery Level:    %8.1f %%                                │\n", battery_percent);
        printf("│ Battery Status:   %-8s                                  │\n", battery_status);
    } else {
        printf("│ ERROR: Unable to read power telemetry data                │\n");
        printf("│ Check I2C connection and device power                     │\n");
    }

    printf("│                                                             │\n");

    /* System section */
    printf("│ SYSTEM                                                      │\n");
    printf("│ CPU Usage:        %8.1f %%                                │\n", sys_metrics->cpu_usage);
    printf("│ Memory Usage:     %8.1f %%                                │\n", sys_metrics->memory_usage);
    if (sys_metrics->fan_available && sys_metrics->fan_rpm >= 0) {
        printf("│ Fan Speed:        %8d RPM (%d%%)                        │\n",
               sys_metrics->fan_rpm, sys_metrics->fan_load);
    } else {
        printf("│ Fan Speed:        Not available                           │\n");
    }

    printf("└─────────────────────────────────────────────────────────────┘\n");
    printf("\n[STAT] Telemetry broadcast ready for OASIS network consumption\n");
}

/**
 * @brief Main application entry point
 */
int main(int argc, char *argv[])
{
    /* Command line options */
    const char *i2c_bus = "/dev/i2c-1";
    uint8_t i2c_addr = INA238_BASEADDR;
    float r_shunt = DEFAULT_SHUNT;
    float max_current = DEFAULT_MAX_CURRENT;
    int interval_ms = DEFAULT_SAMPLING_INTERVAL_MS;
    bool service_mode = false;
    
    /* Battery configuration */
    battery_config_t battery_config = battery_configs[1];  // Default to 5S_Li-ion
    bool custom_battery = false;
    
    /* Device and board information */
    ina238_device_t ina238_dev= {0};
    ark_board_info_t ark_info = {0};
    ina238_measurements_t measurements = {0};
    system_metrics_t system_metrics = {0};

    /* MQTT configuration */
    char mqtt_host[128] = MQTT_DEFAULT_HOST;
    int mqtt_port = MQTT_DEFAULT_PORT;
    char mqtt_topic[64] = MQTT_DEFAULT_TOPIC;

    /* Option parsing */
    static struct option long_options[] = {
        {"bus",            required_argument, 0, 'b'},
        {"address",        required_argument, 0, 'a'},
        {"shunt",          required_argument, 0, 's'},
        {"current",        required_argument, 0, 'c'},
        {"interval",       required_argument, 0, 'i'},
        {"battery",        required_argument, 0, 1000},
        {"battery-min",    required_argument, 0, 1001},
        {"battery-max",    required_argument, 0, 1002},
        {"battery-warn",   required_argument, 0, 1003},
        {"battery-crit",   required_argument, 0, 1004},
        {"list-batteries", no_argument,       0, 1005},
        {"mqtt-host",      required_argument, 0, 'H'},
        {"mqtt-port",      required_argument, 0, 'P'},
        {"mqtt-topic",     required_argument, 0, 'T'},
        {"service",        no_argument,       0, 'e'},
        {"help",           no_argument,       0, 'h'},
        {"version",        no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };
    
    /* Try to detect ARK Electronics Jetson Carrier */
    if (ark_detect_jetson_carrier(&ark_info) == 0) {
        OLOG_INFO("ARK Electronics Jetson Carrier detected!");
        ark_print_board_info(&ark_info);
        
        /* Use ARK-specific defaults */
        ark_get_ina238_defaults(&ark_info, &i2c_bus, &r_shunt, &max_current);
        
        OLOG_INFO("Using ARK Jetson Carrier defaults:");
        OLOG_INFO("  I2C Bus: %s", i2c_bus);
        OLOG_INFO("  Shunt: %.3f Ω", r_shunt);
    }
    
    /* Parse command line arguments (can override auto-detected defaults) */
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "b:a:s:c:i:H:P:T:ehv", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'b':
                i2c_bus = optarg;
                break;
            case 'a':
                i2c_addr = (uint8_t)strtol(optarg, NULL, 0);
                break;
            case 's':
                r_shunt = atof(optarg);
                if (r_shunt <= 0.0f) {
                    OLOG_ERROR("Error: Shunt resistance must be positive");
                    return EXIT_FAILURE;
                }
                break;
            case 'c':
                max_current = atof(optarg);
                if (max_current <= 0.0f) {
                    OLOG_ERROR("Error: Maximum current must be positive");
                    return EXIT_FAILURE;
                }
                break;
            case 'i':
                interval_ms = atoi(optarg);
                if (interval_ms < MIN_SAMPLING_INTERVAL_MS || interval_ms > MAX_SAMPLING_INTERVAL_MS) {
                    OLOG_ERROR("Error: Sampling interval must be between %d and %d ms",
                            MIN_SAMPLING_INTERVAL_MS, MAX_SAMPLING_INTERVAL_MS);
                    return EXIT_FAILURE;
                }
                break;
            case 1000: // --battery
                {
                    bool found = false;
                    for (int i = 0; i < NUM_BATTERY_CONFIGS; i++) {
                        if (strcmp(optarg, battery_configs[i].name) == 0) {
                            battery_config = battery_configs[i];
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        OLOG_ERROR("Error: Unknown battery type '%s'", optarg);
                        OLOG_ERROR("Use --list-batteries to see available types");
                        return EXIT_FAILURE;
                    }
                }
                break;
            case 1001: // --battery-min
                battery_config.min_voltage = atof(optarg);
                custom_battery = true;
                strcpy((char*)battery_config.name, "custom");
                break;
            case 1002: // --battery-max
                battery_config.max_voltage = atof(optarg);
                custom_battery = true;
                strcpy((char*)battery_config.name, "custom");
                break;
            case 1003: // --battery-warn
                battery_config.warning_percent = atof(optarg);
                break;
            case 1004: // --battery-crit
                battery_config.critical_percent = atof(optarg);
                break;
            case 1005: // --list-batteries
                print_battery_configs();
                return EXIT_SUCCESS;
            case 'H':  // mqtt-host
                strncpy(mqtt_host, optarg, sizeof(mqtt_host) - 1);
                mqtt_host[sizeof(mqtt_host) - 1] = '\0';
                break;
            case 'P':  // mqtt-port
                mqtt_port = atoi(optarg);
                if (mqtt_port <= 0 || mqtt_port > 65535) {
                    OLOG_ERROR("Error: Invalid MQTT port number");
                    return EXIT_FAILURE;
                }
                break;
            case 'T':  // mqtt-topic
                strncpy(mqtt_topic, optarg, sizeof(mqtt_topic) - 1);
                mqtt_topic[sizeof(mqtt_topic) - 1] = '\0';
                break;
            case 'e':  // service mode
                service_mode = true;
                break;
            case 'v':
                print_version();
                return EXIT_SUCCESS;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    /* Initialize logging based on mode */
    if (service_mode) {
        // Initialize syslog for service mode
        init_syslog("oasis-stat");
        OLOG_INFO("Starting OASIS STAT in service mode");
    } else {
        // Initialize console logging for interactive mode
        init_logging(NULL, LOG_TO_CONSOLE);
    }

    /* Initialize MQTT */
    if (mqtt_init(mqtt_host, mqtt_port, mqtt_topic) != 0) {
        OLOG_WARNING("Warning: Failed to initialize MQTT. Continuing without MQTT support.");
    } else {
        OLOG_INFO("MQTT publishing enabled. Topic: %s", mqtt_topic);
    }

    /* Validate custom battery configuration */
    if (custom_battery && battery_config.max_voltage <= battery_config.min_voltage) {
        OLOG_ERROR("Error: Battery max voltage must be greater than min voltage");
        return EXIT_FAILURE;
    }
    
    /* Initialize signal handler for graceful shutdown */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize the INA238 device */
    if (ina238_init(&ina238_dev, i2c_bus, i2c_addr, r_shunt, max_current) < 0) {
        OLOG_ERROR("Error: Failed to initialize INA238 device");
        return EXIT_FAILURE;
    }

    if (cpu_monitor_init() == 0) {
        OLOG_INFO("CPU monitoring initialized");
    } else {
        OLOG_WARNING("CPU monitoring initialization failed");
    }

    if (memory_monitor_init() == 0) {
        OLOG_INFO("Memory monitoring initialized");
    } else {
        OLOG_WARNING("Memory monitoring initialization failed");
    }

    if (fan_monitor_init() == 0) {
        system_metrics.fan_available = true;
        OLOG_INFO("Fan monitoring initialized");
    } else {
        OLOG_WARNING("Fan monitoring initialization failed");
    }
    
    /* Print device status */
    ina238_print_status(&ina238_dev);
    
    /* Main monitoring loop */
    while (g_running) {
        float battery_percentage = 0.0F;

        /* Read measurements from INA238 */
        if (ina238_read_measurements(&ina238_dev, &measurements) != 0) {
            measurements.valid = false;
        }

        /* Calculate battery percentage here so it's available for both display and MQTT */
        if (measurements.valid) {
            battery_percentage = calculate_battery_percentage(measurements.bus_voltage, &battery_config);
            mqtt_publish_power_data(&measurements, battery_percentage);
        }

        /* Read CPU usage */
        system_metrics.cpu_usage = cpu_monitor_get_usage();
        mqtt_publish_cpu_data(system_metrics.cpu_usage);

        /* Read memory usage */
        system_metrics.memory_usage = memory_monitor_get_usage();
        mqtt_publish_memory_data(system_metrics.memory_usage);

        /* Read fan metrics */
        if (system_metrics.fan_available) {
            system_metrics.fan_rpm = fan_monitor_get_rpm();
            system_metrics.fan_load = fan_monitor_get_load_percent();

            mqtt_publish_fan_data(system_metrics.fan_rpm, system_metrics.fan_load);
        }

        if (!service_mode) {
            /* Update display */
            print_measurements(&measurements, &ark_info, &battery_config, &system_metrics);
        }

        /* Sleep for specified interval */
        i2c_msleep(interval_ms);
    }
    
    /* Cleanup */
    OLOG_INFO("[STAT] Shutting down telemetry collection...");
    OLOG_INFO("[STAT] OFFLINE - Telemetry collection stopped");
    cpu_monitor_cleanup();
    memory_monitor_cleanup();
    fan_monitor_cleanup();
    mqtt_cleanup();
    ina238_close(&ina238_dev);
    close_logging();
    
    return EXIT_SUCCESS;
}

