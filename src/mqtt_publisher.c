/**
 * @file mqtt_publisher.c
 * @brief MQTT Publishing Functions for OASIS STAT
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mosquitto.h>
#include <json-c/json.h>

#include "mqtt_publisher.h"
#include "logging.h"
#include "ina238.h"
#include "ina3221.h"

/* Forward declaration of battery_config_t */
struct battery_config_t;

/* Static variables */
static struct mosquitto *mosq = NULL;
static bool mqtt_initialized = false;
static char current_topic[64] = MQTT_DEFAULT_TOPIC;

/* MQTT callback functions */
void on_connect(struct mosquitto *mosq, void *obj, int reason_code)
{
   (void)obj; /* Mark parameter as intentionally unused */
   
   if(reason_code != 0){
      OLOG_ERROR("MQTT connection failed: %s", mosquitto_strerror(reason_code));
      mosquitto_disconnect(mosq);
      return;
   }

   OLOG_INFO("MQTT: Connected to broker\n");
}

void on_disconnect(struct mosquitto *mosq, void *obj, int reason_code)
{
   (void)mosq; /* Mark parameter as intentionally unused */
   (void)obj;  /* Mark parameter as intentionally unused */
   
   if (mqtt_initialized) {
       OLOG_ERROR("MQTT: Disconnected from broker: %s", mosquitto_strerror(reason_code));
   } else {
       OLOG_INFO("MQTT: Disconnected from broker: %s", mosquitto_strerror(reason_code));
   }
}

int mqtt_init(const char *host, int port, const char *topic)
{
   int rc;

   /* Store current topic */
   strncpy(current_topic, topic, sizeof(current_topic) - 1);
   current_topic[sizeof(current_topic) - 1] = '\0';

   /* Initialize the mosquitto library */
   mosquitto_lib_init();

   /* Create a new mosquitto client instance */
   mosq = mosquitto_new(NULL, true, NULL);
   if (!mosq) {
      OLOG_ERROR("MQTT: Failed to create client instance");
      return -1;
   }

   /* Set callbacks */
   mosquitto_connect_callback_set(mosq, on_connect);
   mosquitto_disconnect_callback_set(mosq, on_disconnect);

   /* Connect to broker */
   OLOG_INFO("MQTT: Connecting to broker at %s:%d", host, port);
   rc = mosquitto_connect(mosq, host, port, 60);
   if (rc != MOSQ_ERR_SUCCESS) {
      OLOG_ERROR("MQTT: Unable to connect to broker: %s", mosquitto_strerror(rc));
      mosquitto_destroy(mosq);
      mosq = NULL;
      return -1;
   }

   /* Start the mosquitto loop in a background thread */
   rc = mosquitto_loop_start(mosq);
   if (rc != MOSQ_ERR_SUCCESS) {
      OLOG_ERROR("MQTT: Unable to start loop: %s", mosquitto_strerror(rc));
      mosquitto_disconnect(mosq);
      mosquitto_destroy(mosq);
      mosq = NULL;
      return -1;
   }

   mqtt_initialized = true;
   return 0;
}

int mqtt_publish_battery_data(const ina238_measurements_t *measurements,
                          float battery_percentage,
                          const battery_config_t *battery)
{
    if (!mqtt_initialized || !mosq || !measurements->valid) {
        return -1;
    }

    /* Determine battery status */
    const char *battery_status;
    if (battery_percentage <= 10.0f) {
        battery_status = "CRITICAL";
    } else if (battery_percentage <= 20.0f) {
        battery_status = "WARNING";
    } else {
        battery_status = "NORMAL";
    }

    /* Create JSON object */
    struct json_object *root = json_object_new_object();

    /* Add device type and measurements */
    json_object_object_add(root, "device", json_object_new_string("Battery"));
    json_object_object_add(root, "type", json_object_new_string("INA238"));
    json_object_object_add(root, "voltage", json_object_new_double(measurements->bus_voltage));
    json_object_object_add(root, "current", json_object_new_double(measurements->current));
    json_object_object_add(root, "power", json_object_new_double(measurements->power));
    json_object_object_add(root, "temperature", json_object_new_double(measurements->temperature));

    /* Add battery information */
    json_object_object_add(root, "battery_level", json_object_new_double(battery_percentage));
    json_object_object_add(root, "battery_status", json_object_new_string(battery_status));

    /* Add battery time remaining if battery config is available */
    if (battery) {
        battery_state_t state = {
            .voltage = measurements->bus_voltage,
            .current = measurements->current,
            .temperature = measurements->temperature,
            .percent_remaining = battery_percentage,
            .valid = true
        };

        /* Calculate raw time */
        float raw_time = battery_estimate_time_remaining(&state, battery);

        /* Apply smoothing (source_id 0 for INA238) */
        float smoothed_time = smooth_battery_runtime(raw_time, measurements->current, SOURCE_INA238);

        /* Format time */
        int hours = (int)(smoothed_time / 60.0f);
        int minutes = (int)(smoothed_time - hours * 60.0f);
        char time_str[10];
        snprintf(time_str, sizeof(time_str), "%d:%02d", hours, minutes);

        json_object_object_add(root, "time_remaining_min", json_object_new_double(smoothed_time));
        json_object_object_add(root, "time_remaining_fmt", json_object_new_string(time_str));

        /* Add battery configuration details */
        json_object_object_add(root, "battery_chemistry",
                              json_object_new_string(battery_chemistry_to_string(battery->chemistry)));
        json_object_object_add(root, "battery_capacity_mah",
                              json_object_new_double(battery->capacity_mah));
        json_object_object_add(root, "battery_cells",
                              json_object_new_int(battery->cells_series));
    }

    /* Convert to JSON string */
    const char *json_str = json_object_to_json_string(root);

    /* Publish to MQTT */
    int rc = mosquitto_publish(mosq, NULL, current_topic, strlen(json_str), json_str, 0, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        OLOG_ERROR("MQTT: Failed to publish message: %s", mosquitto_strerror(rc));
    }

    /* Free JSON object */
    json_object_put(root);

    return (rc == MOSQ_ERR_SUCCESS) ? 0 : -1;
}

/**
 * @brief Publish INA3221 multi-channel power data to MQTT (simplified)
 *
 * @param measurements INA3221 measurements from all channels
 * @return int 0 on success, negative on error
 */
int mqtt_publish_ina3221_data(const ina3221_measurements_t *measurements)
{
   if (!mqtt_initialized || !mosq || !measurements->valid) {
      return -1;
   }

   /* Create JSON object */
   struct json_object *root = json_object_new_object();
   struct json_object *channels_array = json_object_new_array();

   /* Add device type */
   json_object_object_add(root, "device", json_object_new_string("SystemPower"));
   json_object_object_add(root, "chip", json_object_new_string("INA3221"));
   json_object_object_add(root, "num_channels", json_object_new_int(measurements->num_channels));

   /* Add each channel */
   for (int i = 0; i < measurements->num_channels; i++) {
      const ina3221_channel_t *ch = &measurements->channels[i];

      if (!ch->valid) continue;

      struct json_object *channel_obj = json_object_new_object();
      json_object_object_add(channel_obj, "channel", json_object_new_int(ch->channel));
      json_object_object_add(channel_obj, "label", json_object_new_string(ch->label));
      json_object_object_add(channel_obj, "voltage", json_object_new_double(ch->voltage));
      json_object_object_add(channel_obj, "current", json_object_new_double(ch->current));
      json_object_object_add(channel_obj, "power", json_object_new_double(ch->power));
      json_object_object_add(channel_obj, "shunt_resistor", json_object_new_double(ch->shunt_resistor));

      json_object_array_add(channels_array, channel_obj);
   }

   json_object_object_add(root, "channels", channels_array);

   /* Convert to JSON string */
   const char *json_str = json_object_to_json_string(root);

   /* Publish to MQTT */
   int rc = mosquitto_publish(mosq, NULL, current_topic, strlen(json_str), json_str, 0, false);
   if (rc != MOSQ_ERR_SUCCESS) {
      OLOG_ERROR("MQTT: Failed to publish INA3221 message: %s", mosquitto_strerror(rc));
   }

   /* Free JSON object */
   json_object_put(root);

   return (rc == MOSQ_ERR_SUCCESS) ? 0 : -1;
}

/**
 * @brief Publish Daly BMS data to MQTT
 */
int mqtt_publish_daly_bms_data(const daly_device_t *daly_dev, const battery_config_t *battery)
{
   if (!mqtt_initialized || !mosq || !daly_dev || !daly_dev->initialized || !daly_dev->data.valid) {
      return -1;
   }

   const daly_data_t *data = &daly_dev->data;

   /* Create JSON object */
   struct json_object *root = json_object_new_object();
   struct json_object *cells_array = json_object_new_array();
   struct json_object *temps_array = json_object_new_array();
   struct json_object *faults_array = json_object_new_array();

   /* Add device type */
   json_object_object_add(root, "device", json_object_new_string("Battery"));
   json_object_object_add(root, "type", json_object_new_string("DalyBMS"));

   /* Add pack information */
   json_object_object_add(root, "voltage", json_object_new_double(data->pack.v_total_v));
   json_object_object_add(root, "current", json_object_new_double(data->pack.current_a));
   json_object_object_add(root, "power", json_object_new_double(data->pack.v_total_v * data->pack.current_a));
   json_object_object_add(root, "battery_level", json_object_new_double(data->pack.soc_pct));

   /* Add MOS status */
   json_object_object_add(root, "charge_fet", json_object_new_boolean(data->mos.charge_mos));
   json_object_object_add(root, "discharge_fet", json_object_new_boolean(data->mos.discharge_mos));

   /* Add battery statistics */
   json_object_object_add(root, "cycles", json_object_new_int(data->mos.life_cycles));
   json_object_object_add(root, "remaining_capacity_mah", json_object_new_int(data->mos.remain_capacity_mah));

   /* Add cell information */
   json_object_object_add(root, "battery_cells", json_object_new_int(data->status.cell_count));
   json_object_object_add(root, "vmax", json_object_new_double(data->extremes.vmax_v));
   json_object_object_add(root, "vmax_cell", json_object_new_int(data->extremes.vmax_cell));
   json_object_object_add(root, "vmin", json_object_new_double(data->extremes.vmin_v));
   json_object_object_add(root, "vmin_cell", json_object_new_int(data->extremes.vmin_cell));
   json_object_object_add(root, "vdelta", json_object_new_double(data->extremes.vmax_v - data->extremes.vmin_v));

   /* Add temperature information */
   json_object_object_add(root, "temp_count", json_object_new_int(data->temps.ntc_count));
   json_object_object_add(root, "tmax", json_object_new_double(data->temps.tmax_c));
   json_object_object_add(root, "tmax_sensor", json_object_new_int(data->temps.tmax_idx));
   json_object_object_add(root, "tmin", json_object_new_double(data->temps.tmin_c));
   json_object_object_add(root, "tmin_sensor", json_object_new_int(data->temps.tmin_idx));

   /* Add derived state information */
   int state = daly_bms_infer_state(data->pack.current_a, data->mos.charge_mos,
                                   data->mos.discharge_mos, DALY_CURRENT_DEADBAND);
   json_object_object_add(root, "charging_state", json_object_new_string(
      state == DALY_STATE_CHARGE ? "charging" :
      state == DALY_STATE_DISCHARGE ? "discharging" : "idle"));

   /* Add charger and load presence */
   bool charger_present = daly_bms_infer_charger(data->pack.current_a, data->mos.charge_mos, DALY_CURRENT_DEADBAND);
   bool load_present = daly_bms_infer_load(data->pack.current_a, data->mos.discharge_mos, DALY_CURRENT_DEADBAND);
   json_object_object_add(root, "charger_present", json_object_new_boolean(charger_present));
   json_object_object_add(root, "load_present", json_object_new_boolean(load_present));

   /* Add cell voltages array */
   for (int i = 0; i < data->status.cell_count && i < DALY_MAX_CELLS; i++) {
      struct json_object *cell_obj = json_object_new_object();
      json_object_object_add(cell_obj, "index", json_object_new_int(i + 1));
      json_object_object_add(cell_obj, "voltage", json_object_new_double(data->cell_mv[i] / 1000.0));
      json_object_object_add(cell_obj, "balance", json_object_new_boolean(data->balance[i]));
      json_object_array_add(cells_array, cell_obj);
   }
   json_object_object_add(root, "cells", cells_array);

   /* Add temperature sensors array */
   for (int i = 0; i < data->temps.ntc_count && i < DALY_MAX_TEMPS; i++) {
      struct json_object *temp_obj = json_object_new_object();
      json_object_object_add(temp_obj, "index", json_object_new_int(i + 1));
      json_object_object_add(temp_obj, "temperature", json_object_new_double(data->temps.sensors_c[i]));
      json_object_array_add(temps_array, temp_obj);
   }
   json_object_object_add(root, "temperatures", temps_array);

   /* Add faults array */
   for (int i = 0; i < data->fault_count; i++) {
      json_object_array_add(faults_array, json_object_new_string(data->faults[i]));
   }
   json_object_object_add(root, "faults", faults_array);

   /* Calculate raw time */
   float raw_time = daly_bms_estimate_runtime(daly_dev, battery);

    /* Apply smoothing (source_id 1 for DalyBMS) */
   /* Ensure the current is treated as positive for runtime calculation */
   float current_abs = fabsf(data->pack.current_a);
   float smoothed_time = smooth_battery_runtime(raw_time, current_abs, SOURCE_DALYBMS);

   /* Format time */
   int hours = (int)(smoothed_time / 60.0f);
   int minutes = (int)(smoothed_time - hours * 60.0f);
   char time_str[10];
   snprintf(time_str, sizeof(time_str), "%d:%02d", hours, minutes);

   json_object_object_add(root, "time_remaining_min", json_object_new_double(smoothed_time));
   json_object_object_add(root, "time_remaining_fmt", json_object_new_string(time_str));

   /* Convert to JSON string */
   const char *json_str = json_object_to_json_string(root);

   /* Publish to MQTT */
   int rc = mosquitto_publish(mosq, NULL, current_topic, strlen(json_str), json_str, 0, false);
   if (rc != MOSQ_ERR_SUCCESS) {
      OLOG_ERROR("MQTT: Failed to publish Daly BMS message: %s", mosquitto_strerror(rc));
   }

   /* Free JSON object */
   json_object_put(root);

   return (rc == MOSQ_ERR_SUCCESS) ? 0 : -1;
}

/**
 * @brief Publish enhanced Daly BMS health data to MQTT
 */
int mqtt_publish_daly_health_data(const daly_device_t *daly_dev,
                                 const daly_pack_health_t *health,
                                 const daly_fault_summary_t *fault_summary)
{
   if (!mqtt_initialized || !mosq || !daly_dev || !health || !fault_summary) {
      return -1;
   }

   /* Create JSON object */
   struct json_object *root = json_object_new_object();
   struct json_object *cells_array = json_object_new_array();
   struct json_object *critical_faults_array = json_object_new_array();
   struct json_object *warning_faults_array = json_object_new_array();

   /* Add device type */
   json_object_object_add(root, "device", json_object_new_string("BatteryHealth"));

   /* Add pack health information */
   json_object_object_add(root, "battery_status", json_object_new_string(daly_bms_health_string(health->overall_status)));
   json_object_object_add(root, "status_reason", json_object_new_string(health->status_reason));
   json_object_object_add(root, "vmax", json_object_new_double(health->vmax));
   json_object_object_add(root, "vmin", json_object_new_double(health->vmin));
   json_object_object_add(root, "vdelta", json_object_new_double(health->vdelta));
   json_object_object_add(root, "vavg", json_object_new_double(health->vavg));
   json_object_object_add(root, "problem_cells", json_object_new_int(health->problem_cell_count));
   json_object_object_add(root, "total_cells", json_object_new_int(health->cell_count));
   json_object_object_add(root, "balancing", json_object_new_boolean(daly_bms_is_balancing(daly_dev)));

   /* Add cell health information */
   for (int i = 0; i < health->cell_count; i++) {
      const daly_cell_health_t *cell = &health->cells[i];
      struct json_object *cell_obj = json_object_new_object();

      json_object_object_add(cell_obj, "index", json_object_new_int(cell->cell_index));
      json_object_object_add(cell_obj, "voltage", json_object_new_double(cell->voltage));
      json_object_object_add(cell_obj, "cell_status", json_object_new_string(daly_bms_health_string(cell->status)));
      json_object_object_add(cell_obj, "balancing", json_object_new_boolean(cell->balancing));

      if (cell->status != DALY_HEALTH_NORMAL) {
         json_object_object_add(cell_obj, "reason", json_object_new_string(cell->reason));
      }

      json_object_array_add(cells_array, cell_obj);
   }
   json_object_object_add(root, "cells", cells_array);

   /* Add fault summary */
   json_object_object_add(root, "critical_faults", json_object_new_int(fault_summary->critical_count));
   json_object_object_add(root, "warning_faults", json_object_new_int(fault_summary->warning_count));
   json_object_object_add(root, "info_faults", json_object_new_int(fault_summary->info_count));

   /* Add critical faults array */
   for (int i = 0; i < fault_summary->critical_count; i++) {
      json_object_array_add(critical_faults_array,
                          json_object_new_string(fault_summary->critical_faults[i]));
   }
   json_object_object_add(root, "critical_fault_list", critical_faults_array);

   /* Add warning faults array */
   for (int i = 0; i < fault_summary->warning_count; i++) {
      json_object_array_add(warning_faults_array,
                          json_object_new_string(fault_summary->warning_faults[i]));
   }
   json_object_object_add(root, "warning_fault_list", warning_faults_array);

   /* Add runtime estimation if discharge current is present */
   float current_a = daly_dev->data.pack.current_a;
   if (current_a < -0.1f) {
      /* Create a dummy battery config for time estimation */
      battery_config_t batt_config = {
         .capacity_mah = 10000.0f,  /* Default value, will be overridden if BMS reports capacity */
         .chemistry = BATT_CHEMISTRY_LIION
      };

      /* Estimate runtime */
      float runtime_min = daly_bms_estimate_runtime(daly_dev, &batt_config);

      /* Format time as HH:MM */
      int hours = (int)(runtime_min / 60.0f);
      int minutes = (int)(runtime_min - hours * 60.0f);
      char time_str[10];
      snprintf(time_str, sizeof(time_str), "%d:%02d", hours, minutes);

      json_object_object_add(root, "estimated_runtime_min", json_object_new_double(runtime_min));
      json_object_object_add(root, "estimated_runtime_fmt", json_object_new_string(time_str));
   }

   /* Convert to JSON string */
   const char *json_str = json_object_to_json_string(root);

   /* Publish to MQTT */
   char topic[128];
   snprintf(topic, sizeof(topic), "%s/battery_health", current_topic);

   int rc = mosquitto_publish(mosq, NULL, topic, strlen(json_str), json_str, 0, false);
   if (rc != MOSQ_ERR_SUCCESS) {
      OLOG_ERROR("MQTT: Failed to publish battery health message: %s", mosquitto_strerror(rc));
   }

   /* Free JSON object */
   json_object_put(root);

   return (rc == MOSQ_ERR_SUCCESS) ? 0 : -1;
}

/**
 * @brief Publish unified battery data combining multiple sources
 */
int mqtt_publish_unified_battery(const ina238_measurements_t *ina238_measurements,
                              const daly_device_t *daly_dev,
                              const battery_config_t *battery_config,
                              float max_current)
{
    if (!mqtt_initialized || !mosq) {
        return -1;
    }

    /* Check if we have any valid data */
    bool ina238_valid = (ina238_measurements && ina238_measurements->valid);
    bool daly_valid = (daly_dev && daly_dev->initialized && daly_dev->data.valid);

    if (!ina238_valid && !daly_valid) {
        return -1;
    }

    /* Create JSON object */
    struct json_object *root = json_object_new_object();
    struct json_object *sources_array = json_object_new_array();

    /* Add device type */
    json_object_object_add(root, "device", json_object_new_string("BatteryStatus"));

    /* Add sources */
    if (ina238_valid) {
        json_object_array_add(sources_array, json_object_new_string("INA238"));
    }
    if (daly_valid) {
        json_object_array_add(sources_array, json_object_new_string("DalyBMS"));
    }
    json_object_object_add(root, "sources", sources_array);

    /* Basic measurements - prioritize sources */
    float voltage = 0.0f;
    float current = 0.0f;
    float power = 0.0f;
    float battery_level = 0.0f;
    float temperature = 0.0f;

    /* Voltage: Prefer INA238 for voltage */
    if (ina238_valid) {
        voltage = ina238_measurements->bus_voltage;
    } else if (daly_valid) {
        voltage = daly_dev->data.pack.v_total_v;
    }
    json_object_object_add(root, "voltage", json_object_new_double(voltage));

    /* Current: Prefer INA238 for current (often more accurate) */
    if (ina238_valid) {
        current = ina238_measurements->current;
        power = ina238_measurements->power;
    } else if (daly_valid) {
        current = daly_dev->data.pack.current_a;
        power = daly_dev->data.pack.v_total_v * daly_dev->data.pack.current_a;
    }
    json_object_object_add(root, "current", json_object_new_double(current));
    json_object_object_add(root, "power", json_object_new_double(power));

    /* SOC: Prefer Daly BMS for SOC */
    if (daly_valid) {
        battery_level = daly_dev->data.pack.soc_pct;
    } else if (ina238_valid && battery_config) {
        battery_level = battery_calculate_percentage(ina238_measurements->bus_voltage, battery_config);
    }
    json_object_object_add(root, "battery_level", json_object_new_double(battery_level));

    /* Temperature: Prefer Daly BMS for temperature */
    if (daly_valid && daly_dev->data.temps.tmax_c > -40.0f) {
        temperature = daly_dev->data.temps.tmax_c;
    } else if (ina238_valid) {
        temperature = ina238_measurements->temperature;
    }
    json_object_object_add(root, "temperature", json_object_new_double(temperature));

    /* Add charging state if Daly BMS is available */
    const char *state_str;
    if (daly_valid) {
        int state = daly_bms_infer_state(daly_dev->data.pack.current_a,
                                         daly_dev->data.mos.charge_mos,
                                         daly_dev->data.mos.discharge_mos,
                                         DALY_CURRENT_DEADBAND);

        state_str = state == DALY_STATE_CHARGE ? "charging" :
                   state == DALY_STATE_DISCHARGE ? "discharging" : "idle";
    } else {
        /* For other setups, we cannot detect charging, so we'll assume. */
        state_str = "discharging";
    }
    json_object_object_add(root, "charging_state", json_object_new_string(state_str));

    /* Battery status based on combined data */
    const char *status = "NORMAL";
    char status_reason[128] = "";

    /* Always initialize fault counts to zero */
    json_object_object_add(root, "critical_fault_count", json_object_new_int(0));
    json_object_object_add(root, "warning_fault_count", json_object_new_int(0));
    json_object_object_add(root, "info_fault_count", json_object_new_int(0));

    /* Add empty arrays for faults - they will be populated if any exist */
    struct json_object *critical_faults = json_object_new_array();
    struct json_object *warning_faults = json_object_new_array();
    struct json_object *info_faults = json_object_new_array();

    /* BMS status checking with detailed fault reporting */
    if (daly_valid && daly_dev->data.fault_count > 0) {
        /* Categorize faults by severity */
        daly_fault_summary_t fault_summary = {0};
        daly_bms_categorize_faults(daly_dev, &fault_summary);

        /* Update fault counts */
        json_object_object_add(root, "critical_fault_count",
                              json_object_new_int(fault_summary.critical_count));
        json_object_object_add(root, "warning_fault_count",
                              json_object_new_int(fault_summary.warning_count));
        json_object_object_add(root, "info_fault_count",
                              json_object_new_int(fault_summary.info_count));

        /* Add critical faults */
        for (int i = 0; i < fault_summary.critical_count; i++) {
            json_object_array_add(critical_faults,
                                json_object_new_string(fault_summary.critical_faults[i]));
        }

        /* Add warning faults */
        for (int i = 0; i < fault_summary.warning_count; i++) {
            json_object_array_add(warning_faults,
                                json_object_new_string(fault_summary.warning_faults[i]));
        }

        /* Add info faults */
        for (int i = 0; i < fault_summary.info_count; i++) {
            json_object_array_add(info_faults,
                                json_object_new_string(fault_summary.info_faults[i]));
        }

        /* Update status based on fault severity */
        if (fault_summary.critical_count > 0) {
            status = "CRITICAL";
            snprintf(status_reason, sizeof(status_reason),
                    "BMS reports %d critical fault(s)", fault_summary.critical_count);
        } else if (fault_summary.warning_count > 0) {
            status = "WARNING";
            snprintf(status_reason, sizeof(status_reason),
                    "BMS reports %d warning(s)", fault_summary.warning_count);
        }
    }

    /* Always add the fault arrays to ensure they're cleared when there are no faults */
    json_object_object_add(root, "critical_faults", critical_faults);
    json_object_object_add(root, "warning_faults", warning_faults);
    json_object_object_add(root, "info_faults", info_faults);

    /* Check INA238 values */
    if (ina238_valid) {
        /* Check for potentially dangerous current */
        if (max_current > 0.0f) {
            if (fabs(ina238_measurements->current) > max_current * 0.9f) {
                status = "WARNING";
                snprintf(status_reason, sizeof(status_reason), "Current approaching maximum: %.2fA",
                    ina238_measurements->current);
            }
        }

        /* Check for high temperature */
        if (ina238_measurements->temperature > 70.0f) {
            status = "WARNING";
            snprintf(status_reason, sizeof(status_reason), "High temperature: %.1f°C",
                    ina238_measurements->temperature);
        }
        if (ina238_measurements->temperature > 85.0f) {
            status = "CRITICAL";
            snprintf(status_reason, sizeof(status_reason), "Critical temperature: %.1f°C",
                    ina238_measurements->temperature);
        }

        /* Check for low battery */
        float battery_percentage = battery_calculate_percentage(
                ina238_measurements->bus_voltage, battery_config);
        if (battery_percentage < battery_config->critical_percent) {
            status = "CRITICAL";
            snprintf(status_reason, sizeof(status_reason), "Battery critically low: %.1f%%",
                    battery_percentage);
        } else if (battery_percentage < battery_config->warning_percent) {
            status = "WARNING";
            snprintf(status_reason, sizeof(status_reason), "Battery low: %.1f%%",
                    battery_percentage);
        }
    }

    json_object_object_add(root, "battery_status", json_object_new_string(status));
    if (status_reason[0] != '\0') {
        json_object_object_add(root, "status_reason", json_object_new_string(status_reason));
    }

    /* Add time remaining calculation. */
    float raw_time = 0.0f;
    float current_used = 0.0f;

    /* Always prioritize Daly BMS for capacity */
    if (daly_valid) {
        /* Check if charging */
        if (daly_dev->data.pack.current_a > 0.1f) {
            /* Charging - report a very large time */
            raw_time = 9999.0f;
            current_used = 0.1f; /* Avoid division by zero */
        } else {
            /* Discharging or idle */
            float discharge_current = -daly_dev->data.pack.current_a; /* Convert to positive */

            /* Only calculate if actually discharging */
            if (discharge_current > 0.1f) {
                float capacity_mah = daly_dev->data.mos.remain_capacity_mah;

                /* Use remaining capacity if available, otherwise calculate from percentage */
                if (capacity_mah > 0.0f) {
                    raw_time = (capacity_mah / (discharge_current * 1000.0f)) * 60.0f;
                } else {
                    capacity_mah = battery_config->capacity_mah * (daly_dev->data.pack.soc_pct / 100.0f);
                    raw_time = (capacity_mah / (discharge_current * 1000.0f)) * 60.0f;
                }
                current_used = discharge_current;
            } else {
                /* Idle - report a very large time */
                raw_time = 9999.0f;
                current_used = 0.1f;
            }
        }
    } else if (ina238_valid && battery_config) {
        /* Use INA238 if no BMS is available */
        float capacity_mah = battery_config->capacity_mah *
                          (battery_calculate_percentage(ina238_measurements->bus_voltage, battery_config) / 100.0f);
        float current = ina238_measurements->current;

        /* Only calculate if current is significant */
        if (current > 0.1f) {
            raw_time = (capacity_mah / (current * 1000.0f)) * 60.0f;
            current_used = current;
        } else {
            /* Very low current - report a very large time */
            raw_time = 9999.0f;
            current_used = 0.1f;
        }
    }

    /* Apply smoothing (source_id 2 for unified) */
    float smoothed_time = smooth_battery_runtime(raw_time, current_used, SOURCE_UNIFIED);

    /* Format time as HH:MM */
    int hours = (int)(smoothed_time / 60.0f);
    int minutes = (int)(smoothed_time - hours * 60.0f);
    char time_str[10];
    snprintf(time_str, sizeof(time_str), "%d:%02d", hours, minutes);

    /* Add both numeric and formatted time */
    json_object_object_add(root, "time_remaining_min", json_object_new_double(smoothed_time));
    json_object_object_add(root, "time_remaining_fmt", json_object_new_string(time_str));

    /* Add cell-level data if available */
    if (daly_valid && daly_dev->data.status.cell_count > 0) {
        struct json_object *cells_array = json_object_new_array();

        for (int i = 0; i < daly_dev->data.status.cell_count; i++) {
            struct json_object *cell_obj = json_object_new_object();
            json_object_object_add(cell_obj, "index", json_object_new_int(i + 1));
            json_object_object_add(cell_obj, "voltage",
                                  json_object_new_double(daly_dev->data.cell_mv[i] / 1000.0));
            json_object_object_add(cell_obj, "balance",
                                  json_object_new_boolean(daly_dev->data.balance[i]));
            json_object_array_add(cells_array, cell_obj);
        }

        json_object_object_add(root, "cells", cells_array);
        json_object_object_add(root, "battery_cells",
                             json_object_new_int(daly_dev->data.status.cell_count));
    }

    /* Add battery configuration information */
    if (battery_config) {
        /* Add chemistry */
        json_object_object_add(root, "battery_chemistry",
                             json_object_new_string(battery_chemistry_to_string(battery_config->chemistry)));

        /* Add capacity */
        json_object_object_add(root, "battery_capacity_mah",
                             json_object_new_double(battery_config->capacity_mah));

        /* Add additional battery information */
        json_object_object_add(root, "battery_cells_series",
                             json_object_new_int(battery_config->cells_series));
        json_object_object_add(root, "battery_cells_parallel",
                             json_object_new_int(battery_config->cells_parallel));

        /* Add nominal voltage */
        json_object_object_add(root, "battery_nominal_voltage",
                             json_object_new_double(battery_config->nominal_voltage));
    }

    /* Convert to JSON string */
    const char *json_str = json_object_to_json_string(root);

    /* Publish to MQTT */
    int rc = mosquitto_publish(mosq, NULL, current_topic, strlen(json_str), json_str, 0, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        OLOG_ERROR("MQTT: Failed to publish unified battery message: %s", mosquitto_strerror(rc));
    }

    /* Free JSON object */
    json_object_put(root);

    return (rc == MOSQ_ERR_SUCCESS) ? 0 : -1;
}

/**
 * @brief Publish System monitoring data to MQTT
 *
 * @param cpu_usage CPU usage percentage (0-100)
 * @param memory_usage Memory usage percentage (0-100)
 * @param system_temp System temperature (C)
 * @return int 0 on success, negative on error
 */
int mqtt_publish_system_monitoring_data(float cpu_usage, float memory_usage, float system_temp)
{
   if (!mqtt_initialized || !mosq) {
      return -1;
   }

   /* Create JSON object */
   struct json_object *root = json_object_new_object();

   /* Add device type and measurements */
   json_object_object_add(root, "device", json_object_new_string("SystemMetrics"));
   json_object_object_add(root, "cpu_usage", json_object_new_double(cpu_usage));
   json_object_object_add(root, "memory_usage", json_object_new_double(memory_usage));
   json_object_object_add(root, "system_temp", json_object_new_double(system_temp));

   /* Convert to JSON string */
   const char *json_str = json_object_to_json_string(root);

   /* Publish to MQTT */
   int rc = mosquitto_publish(mosq, NULL, current_topic, strlen(json_str), json_str, 0, false);
   if (rc != MOSQ_ERR_SUCCESS) {
      OLOG_ERROR("MQTT: Failed to publish System Monitoring message: %s", mosquitto_strerror(rc));
   }

   /* Free JSON object */
   json_object_put(root);

   return (rc == MOSQ_ERR_SUCCESS) ? 0 : -1;
}

/**
 * @brief Publish fan monitoring data to MQTT
 *
 * @param rpm Fan speed in RPM
 * @param load_percent Fan load percentage (0-100)
 * @return int 0 on success, negative on error
 */
int mqtt_publish_fan_data(int rpm, int load_percent)
{
   if (!mqtt_initialized || !mosq) {
      return -1;
   }

   /* Skip if fan data is not available */
   if (rpm < 0 || load_percent < 0) {
      return 0;  /* Not an error, just no data */
   }

   /* Create JSON object */
   struct json_object *root = json_object_new_object();

   /* Add device type and measurements */
   json_object_object_add(root, "device", json_object_new_string("Fan"));
   json_object_object_add(root, "rpm", json_object_new_int(rpm));
   json_object_object_add(root, "load", json_object_new_int(load_percent));

   /* Convert to JSON string */
   const char *json_str = json_object_to_json_string(root);

   /* Publish to MQTT */
   int rc = mosquitto_publish(mosq, NULL, current_topic, strlen(json_str), json_str, 0, false);
   if (rc != MOSQ_ERR_SUCCESS) {
      OLOG_ERROR("MQTT: Failed to publish fan message: %s", mosquitto_strerror(rc));
   }

   /* Free JSON object */
   json_object_put(root);

   return (rc == MOSQ_ERR_SUCCESS) ? 0 : -1;
}

void mqtt_cleanup(void)
{
   mqtt_initialized = false;
   if (mosq) {
      mosquitto_loop_stop(mosq, true);
      mosquitto_disconnect(mosq);
      mosquitto_destroy(mosq);
      mosq = NULL;
   }
   mosquitto_lib_cleanup();
}

