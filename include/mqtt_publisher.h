/**
 * @file mqtt_publisher.h
 * @brief MQTT Publishing Functions for OASIS STAT
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef MQTT_PUBLISHER_H
#define MQTT_PUBLISHER_H

#include <stdbool.h>
#include "ina238.h"

typedef struct {
    float min_voltage;      // Empty battery voltage
    float max_voltage;      // Full battery voltage
    float warning_percent;  // Warning threshold percentage
    float critical_percent; // Critical threshold percentage
    const char *name;       // Battery type name
} battery_config_t;

/* MQTT Configuration */
#define MQTT_DEFAULT_HOST    "localhost"
#define MQTT_DEFAULT_PORT    1883
#define MQTT_DEFAULT_TOPIC   "stat"

/**
 * @brief Initialize MQTT connection
 * 
 * @param host MQTT broker hostname or IP
 * @param port MQTT broker port
 * @param topic MQTT topic for publishing
 * @return int 0 on success, negative on error
 */
int mqtt_init(const char *host, int port, const char *topic);

/**
 * @brief Publish power monitoring data to MQTT
 * 
 * @param measurements INA238 measurements
 * @param battery_percentage Calculated battery percentage
 * @return int 0 on success, negative on error
 */
int mqtt_publish_power_data(const ina238_measurements_t *measurements,
                          float battery_percentage);

/**
 * @brief Clean up MQTT resources
 */
void mqtt_cleanup(void);

#endif /* MQTT_PUBLISHER_H */

