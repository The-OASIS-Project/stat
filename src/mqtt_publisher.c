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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mosquitto.h>
#include <json-c/json.h>

#include "mqtt_publisher.h"
#include "logging.h"
#include "ina238.h"

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

/* Make sure this function signature exactly matches the one in mqtt_publisher.h */
int mqtt_publish_power_data(const ina238_measurements_t *measurements,
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
    json_object_object_add(root, "device", json_object_new_string("Power"));
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

        float time_remaining = battery_estimate_time_remaining(&state, battery);

        json_object_object_add(root, "time_remaining_min", json_object_new_double(time_remaining));

        /* Format time as HH:MM for display */
        int hours = (int)(time_remaining / 60.0f);
        int minutes = (int)(time_remaining - hours * 60.0f);
        char time_str[10];
        snprintf(time_str, sizeof(time_str), "%d:%02d", hours, minutes);
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
 * @brief Publish CPU monitoring data to MQTT
 *
 * @param cpu_usage CPU usage percentage (0-100)
 * @return int 0 on success, negative on error
 */
int mqtt_publish_cpu_data(float cpu_usage)
{
   if (!mqtt_initialized || !mosq) {
      return -1;
   }

   /* Create JSON object */
   struct json_object *root = json_object_new_object();

   /* Add device type and measurements */
   json_object_object_add(root, "device", json_object_new_string("CPU"));
   json_object_object_add(root, "usage", json_object_new_double(cpu_usage));

   /* Convert to JSON string */
   const char *json_str = json_object_to_json_string(root);

   /* Publish to MQTT */
   int rc = mosquitto_publish(mosq, NULL, current_topic, strlen(json_str), json_str, 0, false);
   if (rc != MOSQ_ERR_SUCCESS) {
      OLOG_ERROR("MQTT: Failed to publish CPU message: %s", mosquitto_strerror(rc));
   }

   /* Free JSON object */
   json_object_put(root);

   return (rc == MOSQ_ERR_SUCCESS) ? 0 : -1;
}

/**
 * @brief Publish memory monitoring data to MQTT
 *
 * @param memory_usage Memory usage percentage (0-100)
 * @return int 0 on success, negative on error
 */
int mqtt_publish_memory_data(float memory_usage)
{
   if (!mqtt_initialized || !mosq) {
      return -1;
   }

   /* Create JSON object */
   struct json_object *root = json_object_new_object();

   /* Add device type and measurements */
   json_object_object_add(root, "device", json_object_new_string("Memory"));
   json_object_object_add(root, "usage", json_object_new_double(memory_usage));

   /* Convert to JSON string */
   const char *json_str = json_object_to_json_string(root);

   /* Publish to MQTT */
   int rc = mosquitto_publish(mosq, NULL, current_topic, strlen(json_str), json_str, 0, false);
   if (rc != MOSQ_ERR_SUCCESS) {
      OLOG_ERROR("MQTT: Failed to publish memory message: %s", mosquitto_strerror(rc));
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

