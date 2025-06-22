/**
 * @file mqtt_publisher.c
 * @brief MQTT Publishing Functions for OASIS STAT
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mosquitto.h>
#include <json-c/json.h>

#include "mqtt_publisher.h"
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
      fprintf(stderr, "MQTT connection failed: %s\n", mosquitto_strerror(reason_code));
      mosquitto_disconnect(mosq);
      return;
   }

   printf("MQTT: Connected to broker\n");
}

void on_disconnect(struct mosquitto *mosq, void *obj, int reason_code)
{
   (void)mosq; /* Mark parameter as intentionally unused */
   (void)obj;  /* Mark parameter as intentionally unused */
   
   fprintf(stderr, "MQTT: Disconnected from broker: %s\n", mosquitto_strerror(reason_code));
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
      fprintf(stderr, "MQTT: Failed to create client instance\n");
      return -1;
   }

   /* Set callbacks */
   mosquitto_connect_callback_set(mosq, on_connect);
   mosquitto_disconnect_callback_set(mosq, on_disconnect);

   /* Connect to broker */
   printf("MQTT: Connecting to broker at %s:%d\n", host, port);
   rc = mosquitto_connect(mosq, host, port, 60);
   if (rc != MOSQ_ERR_SUCCESS) {
      fprintf(stderr, "MQTT: Unable to connect to broker: %s\n", mosquitto_strerror(rc));
      mosquitto_destroy(mosq);
      mosq = NULL;
      return -1;
   }

   /* Start the mosquitto loop in a background thread */
   rc = mosquitto_loop_start(mosq);
   if (rc != MOSQ_ERR_SUCCESS) {
      fprintf(stderr, "MQTT: Unable to start loop: %s\n", mosquitto_strerror(rc));
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
                          float battery_percentage)
{
   if (!mqtt_initialized || !mosq || !measurements->valid) {
      return -1;
   }

   /* Determine battery status */
   const char *battery_status;
   if (battery_percentage <= 10.0f) {  /* Assume critical threshold is 10% */
      battery_status = "CRITICAL";
   } else if (battery_percentage <= 20.0f) {  /* Assume warning threshold is 20% */
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
   
   /* Since we can't directly access the battery_config fields, we'll have to get this info from main */
   /* We'll just use placeholder values for chemistry, min_voltage, and max_voltage */
   json_object_object_add(root, "chemistry", json_object_new_string("Li-ion"));
   json_object_object_add(root, "min_voltage", json_object_new_double(16.5));
   json_object_object_add(root, "max_voltage", json_object_new_double(21.0));

   /* Convert to JSON string */
   const char *json_str = json_object_to_json_string(root);

   /* Publish to MQTT */
   int rc = mosquitto_publish(mosq, NULL, current_topic, strlen(json_str), json_str, 0, false);
   if (rc != MOSQ_ERR_SUCCESS) {
      fprintf(stderr, "MQTT: Failed to publish message: %s\n", mosquitto_strerror(rc));
   }

   /* Free JSON object */
   json_object_put(root);

   return (rc == MOSQ_ERR_SUCCESS) ? 0 : -1;
}

void mqtt_cleanup(void)
{
   if (mosq) {
      mosquitto_loop_stop(mosq, true);
      mosquitto_disconnect(mosq);
      mosquitto_destroy(mosq);
      mosq = NULL;
   }
   mosquitto_lib_cleanup();
   mqtt_initialized = false;
}

