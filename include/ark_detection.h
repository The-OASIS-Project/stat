/**
 * @file ark_detection.h
 * @brief ARK Electronics Jetson Carrier Detection
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
 * This header provides functionality to detect ARK Electronics Jetson Carrier
 * boards by reading the serial number from the onboard EEPROM.
 */

#ifndef ARK_DETECTION_H
#define ARK_DETECTION_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ARK Electronics Jetson Carrier EEPROM Configuration */
#define ARK_EEPROM_I2C_BUS          "/dev/i2c-7"    ///< I2C bus for EEPROM
#define ARK_EEPROM_ADDRESS          0x58            ///< I2C address of AT24CSW010 EEPROM
#define ARK_EEPROM_WORD_ADDRESS     0x80            ///< Word address for serial number
#define ARK_SERIAL_LENGTH           16              ///< Length of serial number in bytes
#define ARK_SERIAL_STRING_LENGTH    33              ///< Length of hex string + null terminator

/* ARK Jetson Carrier optimal INA238 settings */
#define ARK_DEFAULT_I2C_BUS         "/dev/i2c-7"    ///< Default I2C bus for ARK board
#define ARK_DEFAULT_SHUNT           0.001f          ///< 1mΩ shunt resistor
#define ARK_DEFAULT_MAX_CURRENT     10.0f           ///< 10A maximum current

/**
 * @brief ARK board information structure
 */
typedef struct {
    bool detected;                                  ///< Board detection status
    char serial_number[ARK_SERIAL_STRING_LENGTH];   ///< Serial number as hex string
    const char *i2c_bus;                            ///< Recommended I2C bus
    float shunt_resistance;                         ///< Recommended shunt resistance
    float max_current;                              ///< Recommended max current
} ark_board_info_t;

/* Function Prototypes */

/**
 * @brief Detect ARK Electronics Jetson Carrier board
 * 
 * This function attempts to read the serial number from the AT24CSW010 EEPROM
 * to determine if the system is running on an ARK Electronics Jetson Carrier.
 * 
 * @param board_info Pointer to structure to fill with board information
 * @return int 0 on successful detection, negative on error or not detected
 */
int ark_detect_jetson_carrier(ark_board_info_t *board_info);

/**
 * @brief Read serial number from ARK Jetson Carrier EEPROM
 * 
 * @param serial_number Buffer to store serial number (must be at least ARK_SERIAL_STRING_LENGTH)
 * @return int 0 on success, negative on error
 */
int ark_read_serial_number(char *serial_number);

/**
 * @brief Print ARK board information
 * 
 * @param board_info Pointer to board information structure
 */
void ark_print_board_info(const ark_board_info_t *board_info);

/**
 * @brief Get default INA238 settings for ARK board
 * 
 * @param board_info Pointer to board information structure
 * @param i2c_bus Pointer to store I2C bus path
 * @param shunt_resistance Pointer to store shunt resistance value
 * @param max_current Pointer to store max current value
 */
void ark_get_ina238_defaults(const ark_board_info_t *board_info,
                            const char **i2c_bus,
                            float *shunt_resistance,
                            float *max_current);

#ifdef __cplusplus
}
#endif

#endif /* ARK_DETECTION_H */

