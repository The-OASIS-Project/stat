/**
 * @file ark_detection.c
 * @brief ARK Electronics Jetson Carrier Detection Implementation
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
 * This file implements the functionality to detect ARK Electronics Jetson
 * Carrier boards by reading the serial number from the onboard EEPROM.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "ark_detection.h"
#include "i2c_utils.h"

/**
 * @brief Read serial number from ARK Jetson Carrier EEPROM
 */
int ark_read_serial_number(char *serial_number)
{
    i2c_device_t eeprom_dev;
    uint8_t serial_data[ARK_SERIAL_LENGTH];
    
    if (!serial_number) {
        return -1;
    }
    
    /* Open EEPROM I2C device */
    if (i2c_open_device(&eeprom_dev, ARK_EEPROM_I2C_BUS, ARK_EEPROM_ADDRESS) < 0) {
        return -1;
    }
    
    /* Read serial number using block read operation */
    if (i2c_read_block_data(&eeprom_dev, ARK_EEPROM_WORD_ADDRESS, 
                           serial_data, ARK_SERIAL_LENGTH) < 0) {
        i2c_close_device(&eeprom_dev);
        return -1;
    }
    
    /* Close EEPROM device */
    i2c_close_device(&eeprom_dev);
    
    /* Convert binary data to hex string */
    for (int i = 0; i < ARK_SERIAL_LENGTH; i++) {
        sprintf(&serial_number[i * 2], "%02x", serial_data[i]);
    }
    serial_number[ARK_SERIAL_LENGTH * 2] = '\0';
    
    /* Validate that we got meaningful data */
    bool has_data = false;
    for (int i = 0; i < ARK_SERIAL_LENGTH; i++) {
        if (serial_data[i] != 0x00 && serial_data[i] != 0xFF) {
            has_data = true;
            break;
        }
    }
    
    return has_data ? 0 : -1;
}

/**
 * @brief Detect ARK Electronics Jetson Carrier board
 */
int ark_detect_jetson_carrier(ark_board_info_t *board_info)
{
    if (!board_info) {
        return -1;
    }
    
    /* Initialize board info structure */
    memset(board_info, 0, sizeof(ark_board_info_t));
    
    /* Try to read serial number from EEPROM */
    if (ark_read_serial_number(board_info->serial_number) == 0) {
        board_info->detected = true;
        board_info->i2c_bus = ARK_DEFAULT_I2C_BUS;
        board_info->shunt_resistance = ARK_DEFAULT_SHUNT;
        board_info->max_current = ARK_DEFAULT_MAX_CURRENT;
        return 0;
    } else {
        board_info->detected = false;
        return -1;
    }
}

/**
 * @brief Print ARK board information
 */
void ark_print_board_info(const ark_board_info_t *board_info)
{
    if (!board_info) {
        return;
    }
    
    if (board_info->detected) {
        printf("ARK Jetson Carrier Serial: %s\n", board_info->serial_number);
    } else {
        printf("ARK Jetson Carrier: Not detected\n");
    }
}

/**
 * @brief Get default INA238 settings for ARK board
 */
void ark_get_ina238_defaults(const ark_board_info_t *board_info,
                            const char **i2c_bus,
                            float *shunt_resistance,
                            float *max_current)
{
    if (!board_info || !board_info->detected) {
        return;
    }
    
    if (i2c_bus) {
        *i2c_bus = board_info->i2c_bus;
    }
    
    if (shunt_resistance) {
        *shunt_resistance = board_info->shunt_resistance;
    }
    
    if (max_current) {
        *max_current = board_info->max_current;
    }
}

