/*
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
 * the project author(s).
 *
 * MQTT publisher internal helpers exposed for unit testing. Not part of the
 * public API — only mqtt_publisher.c and test files should include this header.
 */

#ifndef MQTT_PUBLISHER_INTERNAL_H
#define MQTT_PUBLISHER_INTERNAL_H

#include <json-c/json.h>

#include "battery_model.h"
#include "daly_bms.h"
#include "ina238.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the JSON payload for an INA238 battery telemetry message.
 *
 * Pure constructor — no broker interaction. Caller owns the returned object
 * and must call json_object_put() when done.
 *
 * @param measurements INA238 measurements (must be valid).
 * @param battery_percentage SOC percentage (0-100).
 * @param battery Optional battery configuration; if NULL, battery-detail fields
 *                (chemistry, capacity, time remaining) are omitted.
 * @return struct json_object* Newly allocated JSON object, or NULL on error.
 */
struct json_object *build_battery_json(const ina238_measurements_t *measurements,
                                       float battery_percentage,
                                       const battery_config_t *battery);

/**
 * @brief Build the JSON payload for a Daly BMS telemetry message.
 *
 * Pure constructor — no broker interaction. Caller owns the returned object
 * and must call json_object_put() when done.
 *
 * @param daly_dev Daly BMS device with valid data populated.
 * @param battery Optional battery configuration for runtime estimation.
 * @return struct json_object* Newly allocated JSON object, or NULL on error.
 */
struct json_object *build_daly_bms_json(const daly_device_t *daly_dev,
                                        const battery_config_t *battery);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_PUBLISHER_INTERNAL_H */
