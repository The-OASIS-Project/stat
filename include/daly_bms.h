/**
 * @file daly_bms.h
 * @brief Daly Smart BMS UART Communication Interface
 * 
 * This module provides functionality to communicate with Daly Smart BMS
 * using the UART protocol. It supports reading pack statistics, 
 * cell voltages, temperatures, MOS states, and fault conditions.
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
 */

#ifndef DALY_BMS_H
#define DALY_BMS_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol Constants */
#define DALY_START_BYTE         0xA5
#define DALY_HOST_ADDR          0x40  /* "upper computer" */
#define DALY_BMS_ADDR           0x01
#define DALY_LEN_FIXED          0x08
#define DALY_FRAME_LEN          13    /* 13 bytes: hdr + addr + cmd + len + data8 + csum */

#define DALY_DEFAULT_BAUD       9600
#define DALY_DEFAULT_TIMEOUT_MS 500
#define DALY_CURRENT_DEADBAND   0.15  /* Amps, to avoid mode flicker around zero */

/* Maximum supported configuration */
#define DALY_MAX_CELLS          32
#define DALY_MAX_TEMPS          8
#define DALY_MAX_FAULTS         32

/* Command Codes */
#define DALY_CMD_PACK_INFO      0x90  /* Basic pack info: voltage, current, SOC */
#define DALY_CMD_CELL_VOLTAGE   0x91  /* Min/max cell voltage info */
#define DALY_CMD_TEMPERATURE    0x92  /* Min/max temperature info */
#define DALY_CMD_MOS_STATUS     0x93  /* MOS status, cycles, capacity */
#define DALY_CMD_STATUS         0x94  /* Cell count, temp count, charge/discharge status */
#define DALY_CMD_CELL_VOLTAGES  0x95  /* Individual cell voltages */
#define DALY_CMD_TEMPERATURES   0x96  /* Individual temperature sensor values */
#define DALY_CMD_BALANCE_STATUS 0x97  /* Cell balancing status */
#define DALY_CMD_FAULTS         0x98  /* Fault flags */

/* Capacity-related commands */
#define DALY_CMD_READ_CAPACITY  0x50  /* Read rated capacity (community extension) */
#define DALY_CMD_WRITE_CAPACITY 0x10  /* Write rated capacity (community extension) */
#define DALY_CMD_WRITE_SOC      0x21  /* Write SOC (community extension) */

/* BMS State Constants */
#define DALY_STATE_DISCHARGE    1
#define DALY_STATE_CHARGE       2
#define DALY_STATE_IDLE         0

/**
 * @brief Pack summary information
 */
typedef struct {
    float v_total_v;            /**< Total pack voltage in Volts */
    float v_total_cumulative_v; /**< Cumulative voltage in Volts (on some variants) */
    float current_a;            /**< Current in Amps (positive=charge, negative=discharge) */
    float soc_pct;              /**< State of charge percentage (0-100) */
} daly_pack_summary_t;

/**
 * @brief Cell voltage extremes
 */
typedef struct {
    float vmax_v;               /**< Maximum cell voltage in Volts */
    int vmax_cell;              /**< Cell number with maximum voltage */
    float vmin_v;               /**< Minimum cell voltage in Volts */
    int vmin_cell;              /**< Cell number with minimum voltage */
} daly_extremes_t;

/**
 * @brief Temperature information
 */
typedef struct {
    float tmax_c;               /**< Maximum temperature in Celsius */
    int tmax_idx;               /**< Sensor index with maximum temperature */
    float tmin_c;               /**< Minimum temperature in Celsius */
    int tmin_idx;               /**< Sensor index with minimum temperature */
    float sensors_c[DALY_MAX_TEMPS]; /**< Individual temperature sensor readings */
    int ntc_count;              /**< Number of temperature sensors */
} daly_temps_t;

/**
 * @brief MOS state and capacity information
 */
typedef struct {
    int state;                  /**< BMS state */
    bool charge_mos;            /**< Charge MOSFET enabled */
    bool discharge_mos;         /**< Discharge MOSFET enabled */
    int life_cycles;            /**< Number of charge/discharge cycles */
    int remain_capacity_mah;    /**< Remaining capacity in mAh */
} daly_mos_caps_t;

/**
 * @brief Status information
 */
typedef struct {
    int cell_count;             /**< Number of cells */
    int ntc_count;              /**< Number of temperature sensors */
    bool charger_present;       /**< Charger presence raw bit (often unreliable) */
    bool load_present;          /**< Load presence raw bit (often unreliable) */
    int dio_bits;               /**< Digital I/O bits */
} daly_status_t;

/**
 * @brief Complete BMS data
 */
typedef struct {
    daly_pack_summary_t pack;   /**< Pack summary information */
    daly_extremes_t extremes;   /**< Cell voltage extremes */
    daly_temps_t temps;         /**< Temperature information */
    daly_mos_caps_t mos;        /**< MOS state and capacity */
    daly_status_t status;       /**< Status information */
    int cell_mv[DALY_MAX_CELLS]; /**< Individual cell voltages in mV */
    bool balance[DALY_MAX_CELLS]; /**< Cell balancing status */
    char faults[DALY_MAX_FAULTS][64]; /**< Active fault descriptions */
    int fault_count;            /**< Number of active faults */
    time_t last_ok;             /**< Timestamp of last successful update */
    char last_err[128];         /**< Last error message */
    bool valid;                 /**< Data validity flag */
} daly_data_t;

/**
 * @brief Daly BMS device information
 */
typedef struct {
    int fd;                     /**< Serial port file descriptor */
    char port[64];              /**< Serial port path */
    int baud;                   /**< Baud rate */
    int timeout_ms;             /**< Communication timeout in milliseconds */
    bool initialized;           /**< Initialization status */
    daly_data_t data;           /**< Most recent BMS data */
} daly_device_t;

/**
 * @brief Capacity information
 */
typedef struct {
    int rated_capacity_mah;     /**< Rated capacity in mAh */
    int nominal_cell_mv;        /**< Nominal cell voltage in mV */
} daly_capacity_t;

/**
 * @brief Initialize the Daly BMS device
 * 
 * @param dev Pointer to device structure
 * @param port Serial port path (e.g., "/dev/ttyTHS1")
 * @param baud Baud rate
 * @param timeout_ms Communication timeout in milliseconds
 * @return int 0 on success, negative on error
 */
int daly_bms_init(daly_device_t *dev, const char *port, int baud, int timeout_ms);

/**
 * @brief Close the Daly BMS device
 * 
 * @param dev Pointer to device structure
 */
void daly_bms_close(daly_device_t *dev);

/**
 * @brief Poll all data from the Daly BMS
 * 
 * @param dev Pointer to device structure
 * @return int 0 on success, negative on error
 */
int daly_bms_poll(daly_device_t *dev);

/**
 * @brief Read rated capacity from the Daly BMS
 * 
 * @param dev Pointer to device structure
 * @param capacity Pointer to capacity structure to fill
 * @return int 0 on success, negative on error
 */
int daly_bms_read_capacity(daly_device_t *dev, daly_capacity_t *capacity);

/**
 * @brief Write rated capacity to the Daly BMS
 * 
 * @param dev Pointer to device structure
 * @param capacity_mah Rated capacity in mAh
 * @param nominal_cell_mv Nominal cell voltage in mV
 * @return int 0 on success, negative on error
 */
int daly_bms_write_capacity(daly_device_t *dev, int capacity_mah, int nominal_cell_mv);

/**
 * @brief Write SOC to the Daly BMS
 * 
 * @param dev Pointer to device structure
 * @param soc_percent SOC percentage (0-100)
 * @return int 0 on success, negative on error
 */
int daly_bms_write_soc(daly_device_t *dev, float soc_percent);

/**
 * @brief Get the inferred state of the BMS
 * 
 * @param current_a Current in Amps
 * @param chg_mos Charge MOSFET status
 * @param dsg_mos Discharge MOSFET status
 * @param threshold Current deadband
 * @return int State: DALY_STATE_CHARGE, DALY_STATE_DISCHARGE, or DALY_STATE_IDLE
 */
int daly_bms_infer_state(float current_a, bool chg_mos, bool dsg_mos, float threshold);

/**
 * @brief Check if charger is present based on current
 * 
 * @param current_a Current in Amps
 * @param chg_mos Charge MOSFET status
 * @param threshold Current deadband
 * @return bool true if charger is present
 */
bool daly_bms_infer_charger(float current_a, bool chg_mos, float threshold);

/**
 * @brief Check if load is present based on current
 * 
 * @param current_a Current in Amps
 * @param dsg_mos Discharge MOSFET status
 * @param threshold Current deadband
 * @return bool true if load is present
 */
bool daly_bms_infer_load(float current_a, bool dsg_mos, float threshold);

/**
 * @brief Print BMS data in human-readable format
 * 
 * @param dev Pointer to device structure
 */
void daly_bms_print_data(const daly_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* DALY_BMS_H */

