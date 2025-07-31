/**
 * @file battery_model.h
 * @brief Battery modeling and time estimation
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

#ifndef BATTERY_MODEL_H
#define BATTERY_MODEL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum length for battery configuration names
 */
#define BATTERY_NAME_MAX_LENGTH 32

/**
 * @brief Battery information sources. Currently used for smoothing the battery runtime.
 */
typedef enum {
   SOURCE_INA238,
   SOURCE_DALYBMS,
   SOURCE_UNIFIED,
   SOURCE_MAX
} battery_source_t;

/**
 * @brief Battery chemistry types
 */
typedef enum {
    BATT_CHEMISTRY_LIION,     /**< Standard Lithium-Ion (e.g., 18650, 21700) */
    BATT_CHEMISTRY_LIPO,      /**< Lithium Polymer */
    BATT_CHEMISTRY_LIFEPO4,   /**< Lithium Iron Phosphate */
    BATT_CHEMISTRY_NIMH,      /**< Nickel Metal Hydride */
    BATT_CHEMISTRY_LEAD_ACID, /**< Lead Acid */
    BATT_CHEMISTRY_UNKNOWN    /**< Unknown/Custom chemistry */
} battery_chemistry_t;

/**
 * @brief Battery configuration structure
 */
typedef struct {
    float min_voltage;         /**< Empty battery voltage */
    float max_voltage;         /**< Full battery voltage */
    float nominal_voltage;     /**< Nominal voltage */
    float warning_percent;     /**< Warning threshold percentage */
    float critical_percent;    /**< Critical threshold percentage */
    float capacity_mah;        /**< Battery capacity in mAh */
    int cells_series;          /**< Number of cells in series */
    int cells_parallel;        /**< Number of cells in parallel (default 1) */
    battery_chemistry_t chemistry; /**< Battery chemistry type */
    char name[BATTERY_NAME_MAX_LENGTH]; /**< Battery type name */
} battery_config_t;

/**
 * @brief Battery state structure
 */
typedef struct {
    float voltage;             /**< Current battery voltage */
    float current;             /**< Current draw in Amps */
    float temperature;         /**< Battery temperature (C) */
    float percent_remaining;   /**< Battery percent remaining (0-100) */
    float time_remaining_min;  /**< Estimated runtime remaining in minutes */
    const char *status;        /**< Battery status (NORMAL, WARNING, CRITICAL) */
    bool valid;                /**< Whether the battery state is valid */
} battery_state_t;

/**
 * @brief Apply adaptive smoothing to battery runtime calculation
 *
 * @param raw_time_min Raw calculated time in minutes
 * @param current_a Current in amps (used to detect significant changes)
 * @param source_id Identifier for the data source (to track separate states)
 * @return float Smoothed time in minutes
 */
float smooth_battery_runtime(float raw_time_min, float current_a, battery_source_t source_id);

/**
 * @brief Initialize battery configuration with default values
 *
 * @param config Pointer to battery configuration structure to initialize
 * @return int 0 on success, negative on error
 */
int init_battery_config(battery_config_t *config);

/**
 * @brief Calculate battery percentage based on battery chemistry and voltage
 *
 * Uses non-linear discharge curves based on the battery chemistry to
 * provide a more accurate state of charge estimation.
 *
 * @param voltage Current battery voltage
 * @param battery Battery configuration
 * @return float Percentage remaining (0-100%)
 */
float battery_calculate_percentage(float voltage, const battery_config_t *battery);

/**
 * @brief Estimate remaining battery time
 *
 * Calculates estimated remaining runtime based on current load,
 * battery capacity, and temperature.
 *
 * @param state Current battery state (voltage, current, temperature)
 * @param battery Battery configuration
 * @return float Estimated runtime remaining in minutes
 */
float battery_estimate_time_remaining(const battery_state_t *state,
                                    const battery_config_t *battery);

/**
 * @brief Get human-readable string for battery chemistry
 *
 * @param chemistry Battery chemistry enum value
 * @return const char* String representation
 */
const char* battery_chemistry_to_string(battery_chemistry_t chemistry);

/**
 * @brief Parse battery chemistry string to enum
 *
 * @param chemistry_str Battery chemistry string
 * @return battery_chemistry_t Corresponding enum value
 */
battery_chemistry_t battery_chemistry_from_string(const char *chemistry_str);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_MODEL_H */

