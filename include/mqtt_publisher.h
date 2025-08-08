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

#include "daly_bms.h"
#include "ina238.h"
#include "ina3221.h"
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
 * @brief Publish battery monitoring data to MQTT
 *
 * @param measurements INA238 measurements
 * @param battery_percentage Calculated battery percentage
 * @param battery Battery configuration for time estimation
 * @return int 0 on success, negative on error
 */
int mqtt_publish_battery_data(const ina238_measurements_t *measurements,
                          float battery_percentage,
                          const battery_config_t *battery);

/**
 * @brief Publish INA3221 multi-channel power data to MQTT
 *
 * @param measurements INA3221 measurements from all channels
 * @return int 0 on success, negative on error
 */
int mqtt_publish_ina3221_data(const ina3221_measurements_t *measurements);

/**
 * @brief Publish Daly BMS data to MQTT
 *
 * @param daly_dev Pointer to Daly BMS device
 * @param battery Battery configuration for time estimation
 * @return int 0 on success, negative on error
 */
int mqtt_publish_daly_bms_data(const daly_device_t *daly_dev, const battery_config_t *battery);

/**
 * @brief Publish enhanced Daly BMS health data to MQTT
 *
 * @param daly_dev Pointer to Daly BMS device
 * @param health Pointer to pack health structure
 * @param fault_summary Pointer to fault summary structure
 * @return int 0 on success, negative on error
 */
int mqtt_publish_daly_health_data(const daly_device_t *daly_dev,
                                 const daly_pack_health_t *health,
                                 const daly_fault_summary_t *fault_summary);

/**
 * @brief Publish unified battery data combining multiple sources
 *
 * @param ina238_measurements INA238 measurements (can be NULL)
 * @param daly_dev Daly BMS device (can be NULL)
 * @param battery_config Battery configuration
 * @return int 0 on success, negative on error
 */
int mqtt_publish_unified_battery(const ina238_measurements_t *ina238_measurements,
                              const daly_device_t *daly_dev,
                              const battery_config_t *battery_config,
                              float max_current);

/**
 * @brief Publish System monitoring data to MQTT
 *
 * @param cpu_usage CPU usage percentage (0-100)
 * @param memory_usage Memory usage percentage (0-100)
 * @param system_temp System temperature (C)
 * @return int 0 on success, negative on error
 */
int mqtt_publish_system_monitoring_data(float cpu_usage,
                                       float memory_usage,
                                       float system_temp);

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

