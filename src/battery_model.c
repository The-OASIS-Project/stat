/**
 * @file battery_model.c
 * @brief Battery modeling and time estimation implementation
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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "battery_model.h"
#include "logging.h"

/* Discharge curve points [soc (0-1), voltage per cell] */
typedef struct {
    float soc;
    float voltage;
} discharge_point_t;

/* Note: all of these folowing tables have been adjusted to multiple sources
 * UNDER LOAD, not open circuit.
 */
/* Li-ion discharge curve (typical 18650/21700 cell) */
static const discharge_point_t liion_discharge_curve[] = {
    {0.00, 2.85}, /* 0% - cutoff voltage */
    {0.05, 3.21}, /* 5% */
    {0.10, 3.32}, /* 10% */
    {0.20, 3.43}, /* 20% */
    {0.30, 3.49}, /* 30% */
    {0.40, 3.60}, /* 40% */
    {0.50, 3.68}, /* 50% */
    {0.60, 3.75}, /* 60% */
    {0.70, 3.81}, /* 70% */
    {0.80, 3.89}, /* 80% */
    {0.90, 4.03}, /* 90% */
    {0.95, 4.11}, /* 95% */
    {1.00, 4.17}  /* 100% - fully charged */
};

/* LiPo discharge curve */
static const discharge_point_t lipo_discharge_curve[] = {
    {0.00, 3.15}, /* 0% - cutoff voltage */
    {0.05, 3.26}, /* 5% */
    {0.10, 3.37}, /* 10% */
    {0.20, 3.48}, /* 20% */
    {0.30, 3.59}, /* 30% */
    {0.40, 3.68}, /* 40% */
    {0.50, 3.73}, /* 50% */
    {0.60, 3.78}, /* 60% */
    {0.70, 3.83}, /* 70% */
    {0.80, 3.91}, /* 80% */
    {0.90, 4.05}, /* 90% */
    {0.95, 4.11}, /* 95% */
    {1.00, 4.17}  /* 100% - fully charged */
};

/* LiFePO4 discharge curve */
static const discharge_point_t lifepo4_discharge_curve[] = {
    {0.00, 2.43}, /* 0% - cutoff voltage */
    {0.05, 2.84}, /* 5% */
    {0.10, 3.04}, /* 10% */
    {0.20, 3.15}, /* 20% */
    {0.30, 3.20}, /* 30% */
    {0.40, 3.24}, /* 40% */
    {0.50, 3.26}, /* 50% - very flat curve */
    {0.60, 3.29}, /* 60% - very flat curve */
    {0.70, 3.31}, /* 70% - very flat curve */
    {0.80, 3.33}, /* 80% */
    {0.90, 3.36}, /* 90% */
    {0.95, 3.38}, /* 95% */
    {1.00, 3.38}  /* 100% - fully charged */
};

/**
 * @brief Initialize battery configuration with default values
 *
 * @param config Pointer to battery configuration structure to initialize
 * @return int 0 on success, negative on error
 */
int init_battery_config(battery_config_t *config)
{
    if (!config) {
        return -1;  // Invalid parameter
    }

    // Initialize with sensible defaults
    config->min_voltage = 0.0f;
    config->max_voltage = 0.0f;
    config->nominal_voltage = 0.0f;
    config->warning_percent = 20.0f;
    config->critical_percent = 10.0f;
    config->capacity_mah = 0.0f;
    config->cells_series = 0;
    config->cells_parallel = 1;
    config->chemistry = BATT_CHEMISTRY_UNKNOWN;

    // Use strncpy to avoid buffer overflow
    strncpy(config->name, "uninitialized", BATTERY_NAME_MAX_LENGTH - 1);
    config->name[BATTERY_NAME_MAX_LENGTH - 1] = '\0'; // Ensure null termination

    return 0;
}

typedef struct { float t; float f; } tf_pair_t;

/* -------------------------------------------------------------------------- */
/*                    25 °C-anchored capacity-retention tables                */
/* -------------------------------------------------------------------------- */

static const tf_pair_t tf_liion[] =   {{ 25,1.00f},{  0,0.88f},{-10,0.74f},
                                       {-20,0.55f},{-30,0.40f}};
static const tf_pair_t tf_lipo[]  =   {{ 25,1.00f},{  0,0.90f},{-10,0.78f},
                                       {-20,0.60f},{-30,0.45f}};
static const tf_pair_t tf_lifepo4[] = {{ 25,1.00f},{  0,0.72f},{-10,0.60f},
                                       {-20,0.45f},{-30,0.35f}};
static const tf_pair_t tf_nimh[]  =   {{ 25,1.00f},{  0,0.85f},{-10,0.70f},
                                       {-20,0.55f},{-30,0.40f}};
static const tf_pair_t tf_lead[]  =   {{ 25,1.00f},{  0,0.46f},{-10,0.40f},
                                       {-20,0.30f},{-30,0.20f}};

// helper for linear interpolation
static float interp_tbl(const tf_pair_t *tbl, size_t n, float Tc)
{
    if (Tc >= tbl[0].t) return tbl[0].f;               /* ≥25 °C → 1.0 */
    if (Tc <= tbl[n-1].t) return tbl[n-1].f;           /* clamp ≤ –30 °C */

    /* find bounding segment (tables are monotonically decreasing in T) */
    for (size_t i = 1; i < n; ++i) {
        if (Tc >= tbl[i].t) {
            /* linear interpolation  f = f0 + (f1-f0)*(ΔT/ΔT_seg) */
            float t0 = tbl[i].t, f0 = tbl[i].f;
            float t1 = tbl[i-1].t, f1 = tbl[i-1].f;
            return f0 + (f1 - f0) * (Tc - t0) / (t1 - t0);
        }
    }
    return 1.0f; /* should never hit */
}

// temperature derate factor (0 – 1) at 25 °C ref
static float battery_temp_capacity_factor(const battery_config_t *cfg, float temp_c)
{
    switch (cfg->chemistry) {
        case BATT_CHEMISTRY_LIION:     return interp_tbl(tf_liion,
                                             sizeof tf_liion/sizeof tf_liion[0], temp_c);
        case BATT_CHEMISTRY_LIPO:      return interp_tbl(tf_lipo,
                                             sizeof tf_lipo/sizeof tf_lipo[0],  temp_c);
        case BATT_CHEMISTRY_LIFEPO4:   return interp_tbl(tf_lifepo4,
                                             sizeof tf_lifepo4/sizeof tf_lifepo4[0], temp_c);
        case BATT_CHEMISTRY_NIMH:      return interp_tbl(tf_nimh,
                                             sizeof tf_nimh/sizeof tf_nimh[0],  temp_c);
        case BATT_CHEMISTRY_LEAD_ACID: return interp_tbl(tf_lead,
                                             sizeof tf_lead/sizeof tf_lead[0],  temp_c);
        default: /* unknown: fall back to Li-ion table as “least wrong” */
            return interp_tbl(tf_liion,
                              sizeof tf_liion/sizeof tf_liion[0], temp_c);
    }
}

/**
 * @brief Calculate cell voltage from battery voltage
 *
 * @param voltage Battery voltage
 * @param cells_series Number of cells in series
 * @return float Cell voltage
 */
static float get_cell_voltage(float voltage, int cells_series)
{
    if (cells_series <= 0) {
        return voltage; /* Invalid configuration, return as-is */
    }

    return voltage / (float)cells_series;
}

/**
 * @brief Interpolate state of charge from discharge curve
 *
 * @param cell_voltage Cell voltage
 * @param curve Discharge curve points
 * @param curve_size Number of points in curve
 * @return float State of charge (0.0-1.0)
 */
static float interpolate_soc(float cell_voltage,
                           const discharge_point_t *curve,
                           size_t curve_size)
{
    /* Handle edge cases */
    if (cell_voltage <= curve[0].voltage) {
        return 0.0f;
    }

    if (cell_voltage >= curve[curve_size-1].voltage) {
        return 1.0f;
    }

    /* Find surrounding points for interpolation */
    for (size_t i = 0; i < curve_size - 1; i++) {
        if (cell_voltage >= curve[i].voltage && cell_voltage <= curve[i+1].voltage) {
            /* Linear interpolation between points */
            float soc_range = curve[i+1].soc - curve[i].soc;
            float voltage_range = curve[i+1].voltage - curve[i].voltage;
            float position = (cell_voltage - curve[i].voltage) / voltage_range;

            return curve[i].soc + position * soc_range;
        }
    }

    /* Should never reach here, but just in case */
    return 0.5f;
}

/**
 * @brief Apply adaptive smoothing to battery runtime calculation
 */
float smooth_battery_runtime(float raw_time_min, float current_a, battery_source_t source_id)
{
    /* State variables for each source (0=INA238, 1=DalyBMS, 2=Unified) */
    static float smoothed_times[SOURCE_MAX] = {0};
    static float previous_currents[SOURCE_MAX] = {0};
    static time_t last_significant_change[SOURCE_MAX] = {0};
    static bool initialized[SOURCE_MAX] = {false};

    /* Ensure valid source_id */
    if (source_id < 0 || source_id >= SOURCE_MAX) {
        source_id = 0;
    }

    /* Initialize on first call for this source */
    if (!initialized[source_id]) {
        smoothed_times[source_id] = raw_time_min;
        previous_currents[source_id] = current_a;
        last_significant_change[source_id] = time(NULL);
        initialized[source_id] = true;
        return raw_time_min;
    }

    /* Base smoothing factor */
    float alpha_base = 0.1f;  /* 10% weight to new reading */
    float alpha = alpha_base;

    /* Calculate current change percentage (use fabs to handle negative currents) */
    float prev_current = previous_currents[source_id];
    float current_change_percent = 0.0f;

    /* Avoid division by zero or near-zero */
    if (fabs(prev_current) > 0.1f) {
        current_change_percent = fabs(current_a - prev_current) / fabs(prev_current);
    }

    /* Get time since last significant change */
    time_t now = time(NULL);
    time_t time_since_change = now - last_significant_change[source_id];

    /* Adapt smoothing factor based on change magnitude and time */
    if (current_change_percent > 0.2f) {
        /* Fast adaptation for significant changes (>20%) */
        alpha = 0.5f;
        last_significant_change[source_id] = now;
    } else if (current_change_percent > 0.1f) {
        /* Moderate adaptation for medium changes (>10%) */
        alpha = 0.3f;
        last_significant_change[source_id] = now;
    } else if (time_since_change > 60) {
        /* Gradual increase in adaptation if stable for over 60 seconds */
        alpha = 0.2f;
    }

    /* Apply exponential moving average smoothing */
    float smoothed_time = (alpha * raw_time_min) +
                          ((1.0f - alpha) * smoothed_times[source_id]);

    /* Update state variables */
    smoothed_times[source_id] = smoothed_time;
    previous_currents[source_id] = current_a;

    OLOG_INFO("Battery runtime smoothing (source %d): raw=%.1f, smoothed=%.1f, alpha=%.2f, change=%.1f%%",
             source_id, raw_time_min, smoothed_time, alpha, current_change_percent * 100.0f);

    return smoothed_time;
}

float battery_calculate_percentage(float voltage, const battery_config_t *battery)
{
    if (!battery) {
        return 0.0f;
    }

    /* Special case for custom/unknown batteries - use linear estimation */
    if (battery->chemistry == BATT_CHEMISTRY_UNKNOWN || battery->cells_series <= 0) {
        /* Simple linear calculation */
        float percentage = ((voltage - battery->min_voltage) /
                         (battery->max_voltage - battery->min_voltage)) * 100.0f;

        /* Clamp to 0-100% */
        if (percentage < 0.0f) percentage = 0.0f;
        if (percentage > 100.0f) percentage = 100.0f;

        return percentage;
    }

    /* Get per-cell voltage */
    float cell_voltage = get_cell_voltage(voltage, battery->cells_series);
    float soc = 0.0f;

    /* Select appropriate discharge curve based on chemistry */
    switch (battery->chemistry) {
        case BATT_CHEMISTRY_LIION:
            soc = interpolate_soc(cell_voltage, liion_discharge_curve,
                                sizeof(liion_discharge_curve)/sizeof(discharge_point_t));
            break;

        case BATT_CHEMISTRY_LIPO:
            soc = interpolate_soc(cell_voltage, lipo_discharge_curve,
                                sizeof(lipo_discharge_curve)/sizeof(discharge_point_t));
            break;

        case BATT_CHEMISTRY_LIFEPO4:
            soc = interpolate_soc(cell_voltage, lifepo4_discharge_curve,
                                sizeof(lifepo4_discharge_curve)/sizeof(discharge_point_t));
            break;

        default:
            /* Fall back to linear model for other chemistries */
            soc = (cell_voltage - 3.0f) / (4.2f - 3.0f); /* Assume Li-ion range */
            break;
    }

    /* Convert to percentage and clamp */
    float percentage = soc * 100.0f;
    if (percentage < 0.0f) percentage = 0.0f;
    if (percentage > 100.0f) percentage = 100.0f;

    return percentage;
}

float battery_estimate_time_remaining(const battery_state_t *state,
                                    const battery_config_t *battery)
{
    if (!state || !battery || !state->valid) {
        return 0.0f;
    }

    /* If no current is being drawn, report a very long time remaining */
    if (state->current <= 0.01f) {
        return 999.0f; /* Essentially infinite */
    }

    /* Get effective capacity based on temperature */
    float effective_capacity = battery->capacity_mah;

    /* Apply temperature compensation if temperature is available */
    if (state->temperature > -100.0f) { /* Valid temperature reading */
        float temp_factor = battery_temp_capacity_factor(battery, state->temperature);
        effective_capacity *= temp_factor;
    }

    /* Calculate remaining capacity in mAh */
    float remaining_capacity = effective_capacity * (state->percent_remaining / 100.0f);

    /* Calculate time remaining in hours, then convert to minutes */
    float time_hours = remaining_capacity / (state->current * 1000.0f);
    float time_minutes = time_hours * 60.0f;

    /* Cap at reasonable values */
    if (time_minutes < 0.0f) {
        time_minutes = 0.0f;
    } else if (time_minutes > 9999.0f) {
        time_minutes = 9999.0f;
    }

    return time_minutes;
}

const char* battery_chemistry_to_string(battery_chemistry_t chemistry)
{
    switch (chemistry) {
        case BATT_CHEMISTRY_LIION:   return "Li-ion";
        case BATT_CHEMISTRY_LIPO:    return "LiPo";
        case BATT_CHEMISTRY_LIFEPO4: return "LiFePO4";
        case BATT_CHEMISTRY_NIMH:    return "NiMH";
        case BATT_CHEMISTRY_LEAD_ACID: return "Lead-Acid";
        case BATT_CHEMISTRY_UNKNOWN:
        default:                     return "Unknown";
    }
}

battery_chemistry_t battery_chemistry_from_string(const char *chemistry_str)
{
    if (!chemistry_str) {
        return BATT_CHEMISTRY_UNKNOWN;
    }

    if (strcasecmp(chemistry_str, "li-ion") == 0 ||
        strcasecmp(chemistry_str, "liion") == 0) {
        return BATT_CHEMISTRY_LIION;
    }

    if (strcasecmp(chemistry_str, "lipo") == 0 ||
        strcasecmp(chemistry_str, "li-po") == 0) {
        return BATT_CHEMISTRY_LIPO;
    }

    if (strcasecmp(chemistry_str, "lifepo4") == 0 ||
        strcasecmp(chemistry_str, "life") == 0) {
        return BATT_CHEMISTRY_LIFEPO4;
    }

    if (strcasecmp(chemistry_str, "nimh") == 0 ||
        strcasecmp(chemistry_str, "ni-mh") == 0) {
        return BATT_CHEMISTRY_NIMH;
    }

    if (strcasecmp(chemistry_str, "lead-acid") == 0 ||
        strcasecmp(chemistry_str, "sla") == 0 ||
        strcasecmp(chemistry_str, "pb") == 0) {
        return BATT_CHEMISTRY_LEAD_ACID;
    }

    return BATT_CHEMISTRY_UNKNOWN;
}
