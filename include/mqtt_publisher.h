/**
 * @file mqtt_publisher.h
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

#ifndef MQTT_PUBLISHER_H
#define MQTT_PUBLISHER_H

#include <stdbool.h>
#include "ina238.h"
#include "battery_model.h"

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
 * @param battery Battery configuration for time estimation
 * @return int 0 on success, negative on error
 */
int mqtt_publish_power_data(const ina238_measurements_t *measurements,
                          float battery_percentage,
                          const battery_config_t *battery);

/**
 * @brief Publish CPU monitoring data to MQTT
 *
 * @param cpu_usage CPU usage percentage (0-100)
 * @return int 0 on success, negative on error
 */
int mqtt_publish_cpu_data(float cpu_usage);

/**
 * @brief Publish memory monitoring data to MQTT
 *
 * @param memory_usage Memory usage percentage (0-100)
 * @return int 0 on success, negative on error
 */
int mqtt_publish_memory_data(float memory_usage);

/**
 * @brief Publish fan monitoring data to MQTT
 *
 * @param rpm Fan speed in RPM
 * @param load_percent Fan load percentage (0-100)
 * @return int 0 on success, negative on error
 */
int mqtt_publish_fan_data(int rpm, int load_percent);

/**
 * @brief Clean up MQTT resources
 */
void mqtt_cleanup(void);

#endif /* MQTT_PUBLISHER_H */

