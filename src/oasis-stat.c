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
#include "daly_bms.h"
#include "fan_monitor.h"
#include "i2c_utils.h"
#include "ina238.h"
#include "ina3221.h"
#include "logging.h"
#include "memory_monitor.h"
#include "mqtt_publisher.h"

/* Application Configuration */
#define DEFAULT_SAMPLING_INTERVAL_MS    1000
#define MIN_SAMPLING_INTERVAL_MS        100
#define MAX_SAMPLING_INTERVAL_MS        10000

typedef enum {
   BAT_4S_LI_ION,
   BAT_5S_LI_ION,
   BAT_6S_LI_ION,
   BAT_2S_LIPO,
   BAT_3S_LIPO,
   BAT_6S_LIPO,
   BAT_4S2P_SAMSUNG50E,
   BAT_3S_5200MAH_LIPO,
   BAT_3S_2200MAH_LIPO,
   BAT_3S_1500MAH_LIPO
} stat_battery_t;

/* Predefined battery configurations */
static const battery_config_t battery_configs[] = {
    /* Standard Li-ion configurations */
    {12.0f, 16.8f, 14.4f, 20.0f, 10.0f, 2600.0f, 4, 1, BATT_CHEMISTRY_LIION, "4S_Li-ion"},     // 4S 18650
    {15.0f, 21.0f, 18.0f, 20.0f, 10.0f, 2600.0f, 5, 1, BATT_CHEMISTRY_LIION, "5S_Li-ion"},     // 5S 18650
    {18.0f, 25.2f, 21.6f, 20.0f, 10.0f, 2600.0f, 6, 1, BATT_CHEMISTRY_LIION, "6S_Li-ion"},     // 6S 18650

    /* LiPo configurations */
    {6.0f,  8.4f,  7.4f,  20.0f, 10.0f, 5000.0f, 2, 1, BATT_CHEMISTRY_LIPO, "2S_LiPo"},        // 2S LiPo
    {9.0f, 12.6f, 11.1f,  20.0f, 10.0f, 5000.0f, 3, 1, BATT_CHEMISTRY_LIPO, "3S_LiPo"},        // 3S LiPo
    {18.0f, 25.2f, 22.2f, 20.0f, 10.0f, 5000.0f, 6, 1, BATT_CHEMISTRY_LIPO, "6S_LiPo"},        // 6S LiPo

    /* User-requested specific configurations */
    {12.0f, 16.8f, 14.4f, 20.0f, 10.0f, 10000.0f, 4, 2, BATT_CHEMISTRY_LIION, "4S2P_Samsung50E"}, // 4S2P Samsung 50E
    {9.0f, 12.6f, 11.1f,  20.0f, 10.0f, 5200.0f,  3, 1, BATT_CHEMISTRY_LIPO, "3S_5200mAh_LiPo"}, // 3S 5200mAh LiPo
    {9.0f, 12.6f, 11.1f,  20.0f, 10.0f, 2200.0f,  3, 1, BATT_CHEMISTRY_LIPO, "3S_2200mAh_LiPo"}, // 3S 2200mAh LiPo
    {9.0f, 12.6f, 11.1f,  20.0f, 10.0f, 1500.0f,  3, 1, BATT_CHEMISTRY_LIPO, "3S_1500mAh_LiPo"}, // 3S 1500mAh LiPo
};

#define NUM_BATTERY_CONFIGS ((int)(sizeof(battery_configs) / sizeof(battery_configs[0])))

/* STAT Version Information */
#define STAT_VERSION_MAJOR 1
#define STAT_VERSION_MINOR 0
#define STAT_VERSION_PATCH 0

typedef enum {
    POWER_MONITOR_NONE,
    POWER_MONITOR_INA238,
    POWER_MONITOR_INA3221,
    POWER_MONITOR_BOTH
} power_monitor_type_t;

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
static bool bms_enable = false;
static char bms_port[64];
static int bms_baud = DALY_DEFAULT_BAUD;
static int bms_interval_ms = 1000;
static int bms_capacity = 0;
static float bms_soc = -1.0f;
static int cell_warning_threshold_mv = DALY_CELL_WARNING_THRESHOLD_MV;
static int cell_critical_threshold_mv = DALY_CELL_CRITICAL_THRESHOLD_MV;

/* Function Prototypes */
static void print_usage(const char *prog_name);
static void print_version(void);
static void print_battery_configs(void);
static void signal_handler(int signal);
static void print_ina238_measurements(const ina238_measurements_t *measurements,
                              const battery_config_t *battery);
static const char* get_battery_status(float percentage, const battery_config_t *battery);
static void print_ina3221_measurements(const ina3221_measurements_t *ina3221_measurements);

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
    printf("  -m, --monitor TYPE     Power monitor type: ina238, ina3221, both, auto (default: auto)\n");
    printf("\nPower Monitor Types:\n");
    printf("  auto    - Automatically detect available power monitors (default)\n");
    printf("  ina238  - Use INA238 single-channel power monitor (I2C direct)\n");
    printf("  ina3221 - Use INA3221 3-channel power monitor (sysfs/hwmon)\n");
    printf("  both    - Use both INA238 and INA3221 simultaneously\n\n");
    printf("      --battery TYPE     Battery type (default: 5S_Li-ion)\n");
    printf("      --battery-min V    Custom battery minimum voltage\n");
    printf("      --battery-max V    Custom battery maximum voltage\n");
    printf("      --battery-warn %%   Battery warning threshold percent (default: 20)\n");
    printf("      --battery-crit %%   Battery critical threshold percent (default: 10)\n");
    printf("      --battery-capacity MAH Battery capacity in mAh\n");
    printf("      --battery-chemistry TYPE Battery chemistry (Li-ion, LiPo, LiFePO4, NiMH, Lead-Acid)\n");
    printf("      --battery-cells NUM      Number of cells in series\n");
    printf("      --battery-parallel NUM   Number of cells in parallel (default: 1)\n");
    printf("      --list-batteries   Show available battery configurations\n");
    printf("  -e, --service          Run in service mode (use with systemd)\n");
    printf("  -h, --help             Show this help message\n");
    printf("  -v, --version          Show version information\n");
    printf("MQTT Options:\n");
    printf("  -H, --mqtt-host HOST   MQTT broker hostname (default: %s)\n", MQTT_DEFAULT_HOST);
    printf("  -P, --mqtt-port PORT   MQTT broker port (default: %d)\n", MQTT_DEFAULT_PORT);
    printf("  -T, --mqtt-topic TOPIC MQTT topic to publish to (default: %s)\n", MQTT_DEFAULT_TOPIC);
    printf("\nDaly BMS Options:\n");
    printf("      --bms-enable         Enable Daly BMS monitoring\n");
    printf("      --bms-port PORT      Serial port for BMS (default: /dev/ttyTHS1)\n");
    printf("      --bms-baud BAUD      Baud rate (default: %d)\n", DALY_DEFAULT_BAUD);
    printf("      --bms-interval MS    Polling interval in ms (default: 1000)\n");
    printf("      --bms-set-capacity N Set BMS rated capacity in mAh\n");
    printf("      --bms-set-soc PCT    Set BMS state of charge (0-100)\n");
    printf("      --bms-warn-thresh MV Cell voltage warning threshold in mV (default: 70)\n");
    printf("      --bms-crit-thresh MV Cell voltage critical threshold in mV (default: 120)\n");
    printf("\nExamples:\n");
    printf("  ./oasis-stat                           # Auto-detect power monitors\n");
    printf("  ./oasis-stat --monitor ina3221         # Force INA3221 3-channel monitoring\n");
    printf("  ./oasis-stat --monitor ina238          # Force INA238 single-channel monitoring\n");
    printf("  ./oasis-stat --monitor both            # Use both monitors (if available)\n");
    printf("  ./oasis-stat --battery 4S2P_Samsung50E # Use specific battery configuration\n");
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
    printf("\033[2J\033[H"); // Clear screen and move cursor to top

    /* Print header */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  STAT - System Telemetry and Analytics Tracker v%d.%d.%d\n",
           STAT_VERSION_MAJOR, STAT_VERSION_MINOR, STAT_VERSION_PATCH);
    printf("  OASIS Hardware Monitoring and Telemetry Collection\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    if (ark_info->detected) {
        printf("Platform: ARK Jetson Carrier (S/N: %s)\n", ark_info->serial_number);
    } else {
        printf("Platform: Unknown Linux System\n");
    }
    if (battery != NULL) {
      printf("Battery: %s (%.1fV - %.1fV)\n", battery->name, battery->min_voltage, battery->max_voltage);
    }
    printf("Status: ONLINE - Telemetry collection active\n");
    printf("Press Ctrl+C to shutdown STAT\n\n");

    /* Print telemetry data */
    printf("\nSYSTEM TELEMETRY DATA\n");
    printf("━━━━━━━━━━━━━━━━━━━━━\n\n");
}

/**
 * @brief Print system monitoring information
 */
static void print_system_monitoring(const system_metrics_t *sys_metrics)
{
   /* System section */
   printf("SYSTEM MONITORING\n");
   printf("  CPU Usage:    %6.1f%%\n", sys_metrics->cpu_usage);
   printf("  Memory Usage: %6.1f%%\n", sys_metrics->memory_usage);
   if (sys_metrics->fan_available && sys_metrics->fan_rpm >= 0) {
      printf("  Fan Speed:    %6d RPM (%d%%)\n", sys_metrics->fan_rpm, sys_metrics->fan_load);
   } else {
      printf("  Fan Speed:    Not available\n");
   }
   printf("\n");
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
 * @brief Print INA238 measurements to screen (updated to match new style)
 */
static void print_ina238_measurements(const ina238_measurements_t *measurements,
                              const battery_config_t *battery)
{
   /* Power section */
   if (measurements->valid) {
      printf("BATTERY POWER\n");
      printf("  Bus Voltage:   %8.3f V\n", measurements->bus_voltage);
      printf("  Current:       %8.3f A\n", measurements->current);
      printf("  Power:         %8.3f W\n", measurements->power);
      printf("  Temperature:   %8.2f °C (INA238 die)\n", measurements->temperature);

      /* Battery status */
      float battery_percent = battery_calculate_percentage(measurements->bus_voltage, battery);
      const char *battery_status = get_battery_status(battery_percent, battery);

      /* Calculate estimated runtime */
      battery_state_t state = {
         .voltage = measurements->bus_voltage,
         .current = measurements->current,
         .temperature = measurements->temperature,
         .percent_remaining = battery_percent,
         .valid = true
      };
      float time_remaining = battery_estimate_time_remaining(&state, battery);

      /* Format time remaining as hours:minutes */
      int hours = (int)(time_remaining / 60.0f);
      int minutes = (int)(time_remaining - hours * 60.0f);

      printf("  Battery Level: %8.1f%%\n", battery_percent);
      printf("  Time Remaining: %4d:%02d h:m\n", hours, minutes);
      printf("  Battery Status: %s\n", battery_status);
      printf("\n");
   } else {
      printf("POWER: ERROR - Unable to read power telemetry data\n");
      printf("Check I2C connection and device power\n\n");
   }
}

/**
 * @brief Print INA3221 multi-channel measurements to screen (no box)
 */
static void print_ina3221_measurements(const ina3221_measurements_t *ina3221_measurements)
{
   /* Multi-channel power section */
   if (ina3221_measurements->valid) {
      printf("POWER MONITORING\n");

      for (int i = 0; i < ina3221_measurements->num_channels; i++) {
         const ina3221_channel_t *ch = &ina3221_measurements->channels[i];
         if (!ch->valid) continue;

         printf("  %s\n", ch->label);
         printf("    Voltage: %8.3f V\n", ch->voltage);
         printf("    Current: %8.3f A\n", ch->current);
         printf("    Power:   %8.3f W\n", ch->power);
         printf("\n");
      }
   } else {
      printf("POWER: ERROR - Unable to read power telemetry data\n");
      printf("Check sysfs interface and device power\n\n");
   }
}

/**
 * @brief Print Daly BMS data to screen
 */
static void print_daly_bms_data(const daly_device_t *daly_dev) {
    if (!daly_dev || !daly_dev->initialized || !daly_dev->data.valid) {
        printf("DALY BMS: Not available or no valid data\n\n");
        return;
    }

    const daly_data_t *data = &daly_dev->data;

    printf("DALY BMS STATUS\n");
    printf("  Voltage:      %8.2f V\n", data->pack.v_total_v);
    printf("  Current:      %8.2f A\n", data->pack.current_a);
    printf("  Power:        %8.2f W\n", data->pack.v_total_v * data->pack.current_a);
    printf("  SOC:          %8.1f%%\n", data->pack.soc_pct);

    /* Derived state */
    int state = daly_bms_infer_state(data->pack.current_a, data->mos.charge_mos,
                                     data->mos.discharge_mos, DALY_CURRENT_DEADBAND);
    printf("  State:        %s\n", state == DALY_STATE_CHARGE ? "Charging" :
                                 state == DALY_STATE_DISCHARGE ? "Discharging" : "Idle");

    printf("  FETs:         CHG=%s  DSG=%s\n",
           data->mos.charge_mos ? "On" : "Off",
           data->mos.discharge_mos ? "On" : "Off");

    printf("  Cells:        %d  (%.3f - %.3f V, Δ=%.3f V)\n",
           data->status.cell_count,
           data->extremes.vmin_v, data->extremes.vmax_v,
           data->extremes.vmax_v - data->extremes.vmin_v);

    /* Show all temperature sensors */
    printf("  Temperatures:  ");
    int temps_per_row = 4;  // Adjust based on display width
    int temp_count = 0;

    for (int i = 0; i < data->temps.ntc_count && i < DALY_MAX_TEMPS; i++) {
        if (data->temps.sensors_c[i] > -40.0f) {  // Filter out invalid temps (-40 is often a default)
            printf("T%d: %.1f°C  ", i+1, data->temps.sensors_c[i]);
            temp_count++;

            // Break line after temps_per_row temperatures
            if (temp_count % temps_per_row == 0 && i < data->temps.ntc_count - 1) {
                printf("\n                ");
            }
        }
    }
    printf("\n");

    printf("  Cycles:       %d\n", data->mos.life_cycles);

    /* Balance status */
    int balance_count = 0;
    for (int i = 0; i < data->status.cell_count; i++) {
        if (data->balance[i]) balance_count++;
    }
    printf("  Balancing:    %d cells\n", balance_count);

    /* Faults */
    if (data->fault_count > 0) {
        printf("  Faults:       %d active faults\n", data->fault_count);
        for (int i = 0; i < data->fault_count && i < 3; i++) { // Show first 3 faults
            printf("    - %s\n", data->faults[i]);
        }
        if (data->fault_count > 3) {
            printf("    - ... and %d more\n", data->fault_count - 3);
        }
    } else {
        printf("  Faults:       None\n");
    }

    printf("\n");
}

/**
 * @brief Print enhanced Daly BMS data to screen
 */
static void print_enhanced_daly_data(const daly_device_t *daly_dev,
                                    const daly_pack_health_t *health,
                                    const daly_fault_summary_t *fault_summary) {
    if (!daly_dev || !daly_dev->initialized || !daly_dev->data.valid || !health) {
        printf("DALY BMS: Not available or no valid data\n\n");
        return;
    }

    const daly_data_t *data = &daly_dev->data;

    printf("DALY BMS STATUS\n");
    printf("  Voltage:      %8.2f V\n", data->pack.v_total_v);
    printf("  Current:      %8.2f A\n", data->pack.current_a);
    printf("  Power:        %8.2f W\n", data->pack.v_total_v * data->pack.current_a);
    printf("  SOC:          %8.1f%%\n", data->pack.soc_pct);

    /* Derived state */
    int state = daly_bms_infer_state(data->pack.current_a, data->mos.charge_mos,
                                     data->mos.discharge_mos, DALY_CURRENT_DEADBAND);
    printf("  State:        %s\n", state == DALY_STATE_CHARGE ? "Charging" :
                                 state == DALY_STATE_DISCHARGE ? "Discharging" : "Idle");

    printf("  FETs:         CHG=%s  DSG=%s\n",
           data->mos.charge_mos ? "On" : "Off",
           data->mos.discharge_mos ? "On" : "Off");

    /* Enhanced health information */
    printf("  Health:       %s", daly_bms_health_string(health->overall_status));
    if (health->overall_status != DALY_HEALTH_NORMAL) {
        printf(" - %s", health->status_reason);
    }
    printf("\n");

    /* Show all cell voltages in a table format */
    printf("  Cell Voltages:\n");
    printf("    ");
    int cells_per_row = 4;  // Adjust based on your typical display width

    for (int i = 0; i < health->cell_count; i++) {
        const daly_cell_health_t *cell = &health->cells[i];
        char status_indicator = ' ';

        // Add status indicator
        if (cell->status == DALY_HEALTH_WARNING) {
            status_indicator = '!';
        } else if (cell->status == DALY_HEALTH_CRITICAL) {
            status_indicator = '*';
        }

        // Print cell with optional balancing indicator
        printf("C%d: %.3fV%c%s  ",
              cell->cell_index,
              cell->voltage,
              status_indicator,
              cell->balancing ? "[B]" : "");

        // Break line after cells_per_row cells
        if ((i + 1) % cells_per_row == 0 && i < health->cell_count - 1) {
            printf("\n    ");
        }
    }
    printf("\n");

    /* Legend for status indicators */
    if (health->problem_cell_count > 0) {
        printf("    Legend: ! = Warning, * = Critical");
        if (daly_bms_is_balancing(daly_dev)) {
            printf(", [B] = Balancing");
        }
        printf("\n");
    }

    /* Show problem cells if any */
    if (health->problem_cell_count > 0) {
        printf("  Problem Cells: %d\n", health->problem_cell_count);
        int shown = 0;
        for (int i = 0; i < health->cell_count && shown < 3; i++) {
            if (health->cells[i].status != DALY_HEALTH_NORMAL) {
                printf("    Cell %-2d: %s - %s\n",
                      health->cells[i].cell_index,
                      daly_bms_health_string(health->cells[i].status),
                      health->cells[i].reason);
                shown++;
            }
        }
        if (health->problem_cell_count > 3) {
            printf("    ... and %d more problem cells\n", health->problem_cell_count - 3);
        }
    }

    /* Show all temperature sensors */
    printf("  Temperatures:  ");
    int temps_per_row = 4;  // Adjust based on display width
    int temp_count = 0;

    for (int i = 0; i < data->temps.ntc_count && i < DALY_MAX_TEMPS; i++) {
        if (data->temps.sensors_c[i] > -40.0f) {  // Filter out invalid temps (-40 is often a default)
            printf("T%d: %.1f°C  ", i+1, data->temps.sensors_c[i]);
            temp_count++;

            // Break line after temps_per_row temperatures
            if (temp_count % temps_per_row == 0 && i < data->temps.ntc_count - 1) {
                printf("\n                ");
            }
        }
    }
    printf("\n");

    printf("  Cycles:       %d%s\n", data->mos.life_cycles,
           data->mos.life_cycles > 250 ? " (Note: cycles roll over at 255)" : "");

    /* Balance status */
    bool balancing = daly_bms_is_balancing(daly_dev);
    printf("  Balancing:    %s\n", balancing ? "Active" : "Inactive");

    /* Faults summary */
    if (fault_summary->critical_count > 0 || fault_summary->warning_count > 0) {
        printf("  Faults:       %d critical, %d warning\n",
              fault_summary->critical_count, fault_summary->warning_count);

        /* Show critical faults first */
        for (int i = 0; i < fault_summary->critical_count && i < 2; i++) {
            printf("    CRITICAL: %s\n", fault_summary->critical_faults[i]);
        }

        /* Then show warnings */
        for (int i = 0; i < fault_summary->warning_count && i < 2; i++) {
            printf("    WARNING:  %s\n", fault_summary->warning_faults[i]);
        }

        /* Indicate if there are more */
        int total_faults = fault_summary->critical_count + fault_summary->warning_count +
                          fault_summary->info_count;
        int shown = MIN(fault_summary->critical_count, 2) + MIN(fault_summary->warning_count, 2);

        if (total_faults > shown) {
            printf("    ... and %d more faults\n", total_faults - shown);
        }
    } else {
        printf("  Faults:       None\n");
    }

    /* Runtime estimation */
    if (data->pack.current_a < -0.1f) {
        /* Create a dummy battery config for time estimation */
        battery_config_t batt_config = {
            .capacity_mah = 10000.0f,  /* Default value, will be overridden if BMS reports capacity */
            .chemistry = BATT_CHEMISTRY_LIION
        };

        /* Estimate runtime */
        float runtime_min = daly_bms_estimate_runtime(daly_dev, &batt_config);
        int hours = (int)(runtime_min / 60.0f);
        int minutes = (int)(runtime_min - hours * 60.0f);

        printf("  Est. Runtime: %d:%02d h:m\n", hours, minutes);
    }

    printf("\n");
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
    power_monitor_type_t power_monitor = POWER_MONITOR_NONE;
    
    /* Battery configuration */
    battery_config_t battery_config;
    bool custom_battery = false;
    
    /* Device and board information */
    ina238_device_t ina238_dev = {0};
    ina3221_device_t ina3221_dev = {0};
    ark_board_info_t ark_info = {0};
    ina238_measurements_t measurements = {0};
    ina3221_measurements_t ina3221_measurements = {0};
    system_metrics_t system_metrics = {0};

    /* MQTT configuration */
    char mqtt_host[128] = MQTT_DEFAULT_HOST;
    int mqtt_port = MQTT_DEFAULT_PORT;
    char mqtt_topic[64] = MQTT_DEFAULT_TOPIC;

    snprintf(bms_port, sizeof(bms_port), "%s", "/dev/ttyTHS1");

    /* Option parsing */
    static struct option long_options[] = {
        {"bus",               required_argument, 0, 'b'},
        {"address",           required_argument, 0, 'a'},
        {"shunt",             required_argument, 0, 's'},
        {"current",           required_argument, 0, 'c'},
        {"interval",          required_argument, 0, 'i'},
        {"monitor",           required_argument, 0, 'm'},
        {"battery",           required_argument, 0, 1000},
        {"battery-min",       required_argument, 0, 1001},
        {"battery-max",       required_argument, 0, 1002},
        {"battery-warn",      required_argument, 0, 1003},
        {"battery-crit",      required_argument, 0, 1004},
        {"list-batteries",    no_argument,       0, 1005},
        {"battery-capacity",  required_argument, 0, 1006},
        {"battery-chemistry", required_argument, 0, 1007},
        {"battery-cells",     required_argument, 0, 1008},
        {"battery-parallel",  required_argument, 0, 1009},
        {"bms-enable",        no_argument,       0, 2000},
        {"bms-port",          required_argument, 0, 2001},
        {"bms-baud",          required_argument, 0, 2002},
        {"bms-interval",      required_argument, 0, 2003},
        {"bms-set-capacity",  required_argument, 0, 2004},
        {"bms-set-soc",       required_argument, 0, 2005},
        {"bms-warn-thresh",   required_argument, 0, 2006},
        {"bms-crit-thresh",   required_argument, 0, 2007},
        {"mqtt-host",         required_argument, 0, 'H'},
        {"mqtt-port",         required_argument, 0, 'P'},
        {"mqtt-topic",        required_argument, 0, 'T'},
        {"service",           no_argument,       0, 'e'},
        {"help",              no_argument,       0, 'h'},
        {"version",           no_argument,       0, 'v'},
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

    // Set default battery config: "4S2P_Samsung50E"
    size_t battery_configs_size = sizeof(battery_configs) / sizeof(battery_configs[0]);
    if (BAT_4S2P_SAMSUNG50E < battery_configs_size) {
        memcpy(&battery_config, &battery_configs[BAT_4S2P_SAMSUNG50E], sizeof(battery_config_t));
    } else {
        OLOG_ERROR("Invalid battery configuration index: %d", BAT_4S2P_SAMSUNG50E);
        exit(EXIT_FAILURE);
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
                strcpy((char*)battery_config.name, "custom");
                battery_config.min_voltage = atof(optarg);
                custom_battery = true;
                break;
            case 1002: // --battery-max
                strcpy((char*)battery_config.name, "custom");
                battery_config.max_voltage = atof(optarg);
                custom_battery = true;
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
            case 1006: // --battery-capacity
                strcpy((char*)battery_config.name, "custom");
                battery_config.capacity_mah = atof(optarg);
                custom_battery = true;
                break;
            case 1007: // --battery-chemistry
                strcpy((char*)battery_config.name, "custom");
                battery_config.chemistry = battery_chemistry_from_string(optarg);
                custom_battery = true;
                break;
            case 1008: // --battery-cells
                strcpy((char*)battery_config.name, "custom");
                battery_config.cells_series = atoi(optarg);
                custom_battery = true;
                break;
            case 1009: // --battery-parallel
                strcpy((char*)battery_config.name, "custom");
                battery_config.cells_parallel = atoi(optarg);
                custom_battery = true;
                break;
            case 2000: // --bms-enable
                bms_enable = true;
                break;
            case 2001: // --bms-port
                snprintf(bms_port, sizeof(bms_port), "%s", optarg);
                break;
            case 2002: // --bms-baud
                bms_baud = atoi(optarg);
                if (bms_baud <= 0) {
                    OLOG_ERROR("Error: Invalid BMS baud rate");
                    return EXIT_FAILURE;
                }
                break;
            case 2003: // --bms-interval
                bms_interval_ms = atoi(optarg);
                if (bms_interval_ms < 100 || bms_interval_ms > 10000) {
                    OLOG_ERROR("Error: BMS interval must be between 100 and 10000 ms");
                    return EXIT_FAILURE;
                }
                break;
            case 2004: // --bms-set-capacity
                bms_capacity = atoi(optarg);
                if (bms_capacity <= 0) {
                    OLOG_ERROR("Error: Invalid BMS capacity");
                    return EXIT_FAILURE;
                }
                break;
            case 2005: // --bms-set-soc
                bms_soc = atof(optarg);
                if (bms_soc < 0.0f || bms_soc > 100.0f) {
                    OLOG_ERROR("Error: BMS SOC must be between 0 and 100");
                    return EXIT_FAILURE;
                }
                break;
            case 2006: // --bms-warn-thresh
                cell_warning_threshold_mv = atoi(optarg);
                if (cell_warning_threshold_mv <= 0) {
                    OLOG_ERROR("Error: Invalid cell warning threshold");
                    return EXIT_FAILURE;
                }
                break;
            case 2007: // --bms-crit-thresh
                cell_critical_threshold_mv = atoi(optarg);
                if (cell_critical_threshold_mv <= 0) {
                    OLOG_ERROR("Error: Invalid cell critical threshold");
                    return EXIT_FAILURE;
                }
                break;
            case 'm': // --monitor
                if (strcmp(optarg, "ina238") == 0) {
                    power_monitor = POWER_MONITOR_INA238;
                } else if (strcmp(optarg, "ina3221") == 0) {
                    power_monitor = POWER_MONITOR_INA3221;
                } else if (strcmp(optarg, "both") == 0) {
                    power_monitor = POWER_MONITOR_BOTH;
                } else if (strcmp(optarg, "auto") == 0) {
                    power_monitor = POWER_MONITOR_NONE; // Auto-detect
                } else {
                    OLOG_ERROR("Error: Unknown monitor type '%s'. Use: ina238, ina3221, both, or auto", optarg);
                    return EXIT_FAILURE;
                }
                break;
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

    /* Auto-detect power monitors if not specified - Check INA3221 first */
    if (power_monitor == POWER_MONITOR_NONE) {
        bool ina238_available = false;
        bool ina3221_available = false;

        OLOG_INFO("Auto-detecting available power monitors...");

        /* Check INA3221 first by looking for sysfs path */
        if (access("/sys/bus/i2c/drivers/ina3221", F_OK) == 0) {
            /* INA3221 driver exists, try to initialize */
            ina3221_device_t test_ina3221;
            if (ina3221_init(&test_ina3221) == 0) {
                ina3221_available = true;
                ina3221_close(&test_ina3221);
                OLOG_INFO("INA3221 detected via sysfs interface");
            } else {
                OLOG_INFO("INA3221 driver found but device not accessible");
            }
        } else {
            OLOG_INFO("INA3221 driver not found in sysfs");
        }

        /* Test INA238 availability (I2C direct access) */
        ina238_device_t test_ina238;
        if (ina238_init(&test_ina238, i2c_bus, i2c_addr, r_shunt, max_current) == 0) {
            ina238_available = true;
            ina238_close(&test_ina238);
            OLOG_INFO("INA238 detected on I2C bus");
        } else {
            OLOG_INFO("INA238 not found or not accessible");
        }

        /* Determine which monitors to use - prefer INA3221 for modern systems */
        if (ina3221_available && ina238_available) {
            power_monitor = POWER_MONITOR_BOTH;
            OLOG_INFO("Auto-selected: Both INA238 and INA3221 available");
        } else if (ina3221_available) {
            power_monitor = POWER_MONITOR_INA3221;
            OLOG_INFO("Auto-selected: INA3221 (3-channel power monitoring)");
        } else if (ina238_available) {
            power_monitor = POWER_MONITOR_INA238;
            OLOG_INFO("Auto-selected: INA238 (single-channel power monitoring)");
        } else {
            OLOG_ERROR("Error: No supported power monitors found");
            OLOG_ERROR("  - INA3221: Check if driver is loaded: ls /sys/bus/i2c/drivers/ina3221/");
            OLOG_ERROR("  - INA238: Check I2C bus %s and address 0x%02X", i2c_bus, i2c_addr);
            return EXIT_FAILURE;
        }
    }

    /* Auto-detect Daly BMS if not explicitly enabled */
    if (!bms_enable) {
        char detected_port[64];
        int detected_baud;

        if (daly_bms_auto_detect(detected_port, &detected_baud)) {
            OLOG_INFO("Auto-detected Daly BMS on %s at %d baud", detected_port, detected_baud);
            bms_enable = true;
            snprintf(bms_port, sizeof(bms_port), "%s", detected_port);
            bms_baud = detected_baud;          /* Use detected baud rate */
        }
    }

    /* Initialize Daly BMS if enabled */
    daly_device_t daly_dev;
    if (bms_enable) {
       /* Initialize BMS */
       if (daly_bms_init(&daly_dev, bms_port, bms_baud, 500) < 0) {
           OLOG_ERROR("Error: Failed to initialize Daly BMS");
           bms_enable = false;
       } else {
           OLOG_INFO("Daly BMS initialized successfully");
           
           /* Handle optional one-time operations */
           if (bms_capacity > 0) {
               OLOG_INFO("Setting Daly BMS capacity to %d mAh", bms_capacity);
               if (daly_bms_write_capacity(&daly_dev, bms_capacity, 3600) < 0) {
                   OLOG_ERROR("Failed to set Daly BMS capacity");
               } else {
                   OLOG_INFO("Daly BMS capacity set successfully");
               }
           }
           
           if (bms_soc >= 0.0f) {
               OLOG_INFO("Setting Daly BMS SOC to %.1f%%", bms_soc);
               if (daly_bms_write_soc(&daly_dev, bms_soc) < 0) {
                   OLOG_ERROR("Failed to set Daly BMS SOC");
               } else {
                   OLOG_INFO("Daly BMS SOC set successfully");
               }
           }
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

    /* Initialize the selected power monitor(s) */
    if (power_monitor == POWER_MONITOR_INA238 || power_monitor == POWER_MONITOR_BOTH) {
        if (ina238_init(&ina238_dev, i2c_bus, i2c_addr, r_shunt, max_current) < 0) {
            OLOG_ERROR("Error: Failed to initialize INA238 device");
            if (power_monitor == POWER_MONITOR_INA238) {
                return EXIT_FAILURE;
            }
        } else {
            OLOG_INFO("INA238 initialized successfully");
        }
    }

    if (power_monitor == POWER_MONITOR_INA3221 || power_monitor == POWER_MONITOR_BOTH) {
        if (ina3221_init(&ina3221_dev) < 0) {
            OLOG_ERROR("Error: Failed to initialize INA3221 device");
            if (power_monitor == POWER_MONITOR_INA3221) {
                return EXIT_FAILURE;
            }
        } else {
            OLOG_INFO("INA3221 initialized successfully");
        }
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
    if (ina238_dev.initialized) {
        ina238_print_status(&ina238_dev);
    }
    if (ina3221_dev.initialized) {
        ina3221_print_status(&ina3221_dev);
    }
    
    static time_t last_bms_poll = 0;
    static daly_pack_health_t bms_health = {0};
    static daly_fault_summary_t bms_faults = {0};
    static bool bms_health_valid = false;

    /* Main monitoring loop */
    while (g_running) {
        float battery_percentage = 0.0F;

        /* Read measurements from INA238 if enabled */
        if (power_monitor == POWER_MONITOR_INA238 || power_monitor == POWER_MONITOR_BOTH) {
            if (ina238_read_measurements(&ina238_dev, &measurements) != 0) {
                measurements.valid = false;
            }

            /* Calculate battery percentage and publish MQTT for INA238 */
            if (measurements.valid) {
                battery_percentage = battery_calculate_percentage(measurements.bus_voltage, &battery_config);
                mqtt_publish_battery_data(&measurements, battery_percentage, &battery_config);
            }
        }

        /* Read measurements from INA3221 if enabled */
        if (power_monitor == POWER_MONITOR_INA3221 || power_monitor == POWER_MONITOR_BOTH) {
            if (ina3221_read_measurements(&ina3221_dev, &ina3221_measurements) != 0) {
                ina3221_measurements.valid = false;
            }

            /* Publish MQTT for INA3221 */
            if (ina3221_measurements.valid) {
                mqtt_publish_ina3221_data(&ina3221_measurements);
            }
        }

        /* Read from Daly BMS if enabled */
        if (bms_enable) {
            time_t now = time(NULL);
            if (now - last_bms_poll >= (bms_interval_ms / 1000)) {
                if (daly_bms_poll(&daly_dev) == 0) {
                    /* Free previous health data if any */
                    if (bms_health_valid && bms_health.cells) {
                        daly_bms_free_health(&bms_health);
                    }

                    /* Analyze battery health */
                    daly_bms_analyze_health(&daly_dev, &bms_health, cell_warning_threshold_mv, cell_critical_threshold_mv);

                    /* Categorize faults */
                    daly_bms_categorize_faults(&daly_dev, &bms_faults);

                    bms_health_valid = true;

                    /* Publish BMS data to MQTT */
                    mqtt_publish_daly_bms_data(&daly_dev, &battery_config);
                    mqtt_publish_daly_health_data(&daly_dev, &bms_health, &bms_faults);

                    last_bms_poll = now;
                }
            }
        }

        /* Now publish the unified data */
        mqtt_publish_unified_battery(
            (power_monitor == POWER_MONITOR_INA238 || power_monitor == POWER_MONITOR_BOTH) ? &measurements : NULL,
            bms_enable ? &daly_dev : NULL,
            &battery_config,
            max_current
        );

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
            if (power_monitor == POWER_MONITOR_INA238 || power_monitor == POWER_MONITOR_BOTH) {
                print_header(&ark_info, &battery_config);
            } else {
                print_header(&ark_info, NULL);
            }

            /* Update display based on which power monitors are active */
            if (power_monitor == POWER_MONITOR_INA238 || power_monitor == POWER_MONITOR_BOTH) {
                print_ina238_measurements(&measurements, &battery_config);
            }

            if (power_monitor == POWER_MONITOR_INA3221 || power_monitor == POWER_MONITOR_BOTH) {
                print_ina3221_measurements(&ina3221_measurements);
            }

            /* Print Daly BMS data if enabled */
            if (bms_enable && bms_health_valid) {
                print_enhanced_daly_data(&daly_dev, &bms_health, &bms_faults);
            } else if (bms_enable) {
                print_daly_bms_data(&daly_dev);
            }

            print_system_monitoring(&system_metrics);

            printf("[STAT] Telemetry broadcast to MQTT subscribers.\n");
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
    if (power_monitor == POWER_MONITOR_INA238 || power_monitor == POWER_MONITOR_BOTH) {
        ina238_close(&ina238_dev);
    }
    if (power_monitor == POWER_MONITOR_INA3221 || power_monitor == POWER_MONITOR_BOTH) {
        ina3221_close(&ina3221_dev);
    }
    if (bms_enable && bms_health_valid && bms_health.cells) {
        daly_bms_free_health(&bms_health);
    }
    if (bms_enable) {
        daly_bms_close(&daly_dev);
    }
    close_logging();
    
    return EXIT_SUCCESS;
}

