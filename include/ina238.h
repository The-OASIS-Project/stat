/**
 * @file ina238.h
 * @brief INA238 Power Monitor Driver Header
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
 *
 * This header defines the interface for the INA238 power monitor driver.
 * The INA238 is a precision digital power monitor with I2C interface.
 */

#ifndef INA238_H
#define INA238_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* INA238 Constants */
#define INA238_BASEADDR             0x45    // Default 7-bit I2C address
#define INA238_MFG_ID_TI            0x5449  // Texas Instruments manufacturer ID
#define INA238_MFG_DIE              0x238   // INA238 device ID

/* Scaling Constants */
#define INA238_DN_MAX               32768.0f        // 2^15
#define INA238_CONST                819.2e6f        // Internal scaling constant
#define INA238_VSCALE               3.125e-03f      // Voltage LSB: 3.125 mV/LSB
#define INA238_TSCALE               7.8125e-03f     // Temperature LSB: 7.8125 mDegC/LSB
#define POWER_LSB_MULTIPLIER        0.2f

/* Default Configuration Values */
#define DEFAULT_MAX_CURRENT         327.68f         // Default max current (A)
#define DEFAULT_SHUNT               0.0003f         // Default shunt resistance (Ohm)

/* ADC Range Settings */
#define INA238_ADCRANGE_SHIFTS      4
#define INA238_ADCRANGE_LOW         (1 << INA238_ADCRANGE_SHIFTS) // ± 40.96 mV
#define INA238_ADCRANGE_HIGH        (0 << INA238_ADCRANGE_SHIFTS) // ±163.84 mV

/**
 * @brief INA238 device configuration structure
 */
typedef struct {
   int fd;                     ///< File descriptor for I2C device
   uint8_t i2c_addr;           ///< I2C address of the device
   float max_current;          ///< Maximum current in Amps
   float rshunt;               ///< Shunt resistor value in Ohms
   float current_lsb;          ///< LSB value for current measurements
   float power_lsb;            ///< LSB value for power measurements
   int16_t range;              ///< ADC range setting
   uint16_t shunt_calibration; ///< Calibration value for shunt
   bool initialized;           ///< Initialization status
} ina238_device_t;

/**
 * @brief INA238 measurement data structure
 */
typedef struct {
   float bus_voltage;    ///< Bus voltage in Volts
   float current;        ///< Current in Amps
   float power;          ///< Power in Watts
   float temperature;    ///< Die temperature in Celsius
   bool valid;           ///< Data validity flag
} ina238_measurements_t;

/* Function Prototypes */

/**
 * @brief Initialize the INA238 device
 * 
 * @param dev Pointer to device structure
 * @param i2c_bus I2C bus device path (e.g., "/dev/i2c-1")
 * @param i2c_addr I2C address of the device
 * @param r_shunt Shunt resistor value in Ohms
 * @param max_current Maximum current in Amps
 * @return int 0 on success, negative on error
 */
int ina238_init(ina238_device_t *dev, const char *i2c_bus, uint8_t i2c_addr, 
                float r_shunt, float max_current);

/**
 * @brief Close the INA238 device
 * 
 * @param dev Pointer to device structure
 */
void ina238_close(ina238_device_t *dev);

/**
 * @brief Read all measurements from INA238
 * 
 * @param dev Pointer to device structure
 * @param measurements Pointer to measurements structure to fill
 * @return int 0 on success, negative on error
 */
int ina238_read_measurements(ina238_device_t *dev, ina238_measurements_t *measurements);

/**
 * @brief Read bus voltage from INA238
 * 
 * @param dev Pointer to device structure
 * @return float Bus voltage in Volts (0.0 on error)
 */
float ina238_read_bus_voltage(ina238_device_t *dev);

/**
 * @brief Read current from INA238
 * 
 * @param dev Pointer to device structure
 * @return float Current in Amps (0.0 on error)
 */
float ina238_read_current(ina238_device_t *dev);

/**
 * @brief Read power from INA238
 * 
 * @param dev Pointer to device structure
 * @return float Power in Watts (0.0 on error)
 */
float ina238_read_power(ina238_device_t *dev);

/**
 * @brief Read die temperature from INA238
 * 
 * @param dev Pointer to device structure
 * @return float Temperature in degrees Celsius (0.0 on error)
 */
float ina238_read_temperature(ina238_device_t *dev);

/**
 * @brief Print device status and configuration
 * 
 * @param dev Pointer to device structure
 */
void ina238_print_status(const ina238_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* INA238_H */

