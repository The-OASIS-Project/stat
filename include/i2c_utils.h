/**
 * @file i2c_utils.h
 * @brief I2C Utility Functions
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
 * This header provides common I2C communication utilities used by
 * the INA238 driver and ARK detection modules.
 */

#ifndef I2C_UTILS_H
#define I2C_UTILS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C device handle structure
 */
typedef struct {
    int fd;             ///< File descriptor for I2C device
    uint8_t address;    ///< I2C slave address
    const char *bus;    ///< I2C bus device path
} i2c_device_t;

/* Function Prototypes */

/**
 * @brief Open I2C device
 * 
 * @param device Pointer to I2C device structure
 * @param bus_path I2C bus device path (e.g., "/dev/i2c-1")
 * @param slave_addr I2C slave address
 * @return int 0 on success, negative on error
 */
int i2c_open_device(i2c_device_t *device, const char *bus_path, uint8_t slave_addr);

/**
 * @brief Close I2C device
 * 
 * @param device Pointer to I2C device structure
 */
void i2c_close_device(i2c_device_t *device);

/**
 * @brief Read 16-bit register from I2C device
 * 
 * @param device Pointer to I2C device structure
 * @param reg_addr Register address
 * @param value Pointer to store the read value
 * @return int 0 on success, negative on error
 */
int i2c_read_register16(i2c_device_t *device, uint8_t reg_addr, uint16_t *value);

/**
 * @brief Write 16-bit value to I2C register
 * 
 * @param device Pointer to I2C device structure
 * @param reg_addr Register address
 * @param value Value to write
 * @return int 0 on success, negative on error
 */
int i2c_write_register16(i2c_device_t *device, uint8_t reg_addr, uint16_t value);

/**
 * @brief Read multiple bytes from I2C device using combined transaction
 * 
 * This function performs a combined write+read I2C transaction, which is
 * equivalent to SMBus block read operations.
 * 
 * @param device Pointer to I2C device structure
 * @param reg_addr Register address to read from
 * @param data Buffer to store read data
 * @param length Number of bytes to read
 * @return int 0 on success, negative on error
 */
int i2c_read_block_data(i2c_device_t *device, uint8_t reg_addr, uint8_t *data, uint8_t length);

/**
 * @brief Read 24-bit register from I2C device
 * 
 * Some registers (like power) are 24-bit. This function reads 3 bytes
 * and combines them into a 32-bit value.
 * 
 * @param device Pointer to I2C device structure
 * @param reg_addr Register address
 * @param value Pointer to store the read value (only lower 24 bits used)
 * @return int 0 on success, negative on error
 */
int i2c_read_register24(i2c_device_t *device, uint8_t reg_addr, uint32_t *value);

/**
 * @brief Byte swap utility for 16-bit values
 * 
 * @param value 16-bit value to swap
 * @return uint16_t Byte-swapped value
 */
static inline uint16_t i2c_swap16(uint16_t value)
{
    return ((value & 0xFF) << 8) | ((value & 0xFF00) >> 8);
}

/**
 * @brief Sleep for specified milliseconds
 * 
 * @param ms Milliseconds to sleep
 */
void i2c_msleep(int ms);

#ifdef __cplusplus
}
#endif

#endif /* I2C_UTILS_H */

