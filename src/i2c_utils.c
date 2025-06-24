/**
 * @file i2c_utils.c
 * @brief I2C Utility Functions Implementation
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
 * This file implements common I2C communication utilities used throughout
 * the application.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#include "i2c_utils.h"
#include "logging.h"

/**
 * @brief Open I2C device
 */
int i2c_open_device(i2c_device_t *device, const char *bus_path, uint8_t slave_addr)
{
    if (!device || !bus_path) {
        return -1;
    }
    
    /* Open I2C bus device */
    device->fd = open(bus_path, O_RDWR);
    if (device->fd < 0) {
        OLOG_ERROR("Failed to open I2C bus %s: %s", bus_path, strerror(errno));
        return -1;
    }
    
    /* Set I2C slave address */
    if (ioctl(device->fd, I2C_SLAVE, slave_addr) < 0) {
        OLOG_ERROR("Failed to set I2C slave address 0x%02X: %s\n",
                slave_addr, strerror(errno));
        close(device->fd);
        device->fd = -1;
        return -1;
    }
    
    device->address = slave_addr;
    device->bus = bus_path;
    
    return 0;
}

/**
 * @brief Close I2C device
 */
void i2c_close_device(i2c_device_t *device)
{
    if (device && device->fd >= 0) {
        close(device->fd);
        device->fd = -1;
        device->address = 0;
        device->bus = NULL;
    }
}

/**
 * @brief Read 16-bit register from I2C device
 */
int i2c_read_register16(i2c_device_t *device, uint8_t reg_addr, uint16_t *value)
{
    if (!device || device->fd < 0 || !value) {
        return -1;
    }
    
    uint8_t buf[2];
    
    /* Write register address */
    if (write(device->fd, &reg_addr, 1) != 1) {
        OLOG_ERROR("Error writing register address 0x%02X: %s",
                reg_addr, strerror(errno));
        return -1;
    }
    
    /* Read register value */
    if (read(device->fd, buf, 2) != 2) {
        OLOG_ERROR("Error reading register 0x%02X: %s",
                reg_addr, strerror(errno));
        return -1;
    }
    
    /* Convert from big-endian */
    *value = (buf[0] << 8) | buf[1];
    
    return 0;
}

/**
 * @brief Write 16-bit value to I2C register
 */
int i2c_write_register16(i2c_device_t *device, uint8_t reg_addr, uint16_t value)
{
    if (!device || device->fd < 0) {
        return -1;
    }
    
    uint8_t buf[3];
    
    buf[0] = reg_addr;
    buf[1] = (value >> 8) & 0xFF;  /* High byte */
    buf[2] = value & 0xFF;         /* Low byte */
    
    if (write(device->fd, buf, 3) != 3) {
        OLOG_ERROR("Error writing to register 0x%02X: %s",
                reg_addr, strerror(errno));
        return -1;
    }
    
    return 0;
}

/**
 * @brief Read multiple bytes from I2C device using combined transaction
 */
int i2c_read_block_data(i2c_device_t *device, uint8_t reg_addr, uint8_t *data, uint8_t length)
{
    if (!device || device->fd < 0 || !data || length == 0) {
        return -1;
    }
    
    struct i2c_rdwr_ioctl_data rdwr_data;
    struct i2c_msg msgs[2];
    
    /* Write message: send the register address */
    msgs[0].addr = device->address;
    msgs[0].flags = 0;  /* Write */
    msgs[0].len = 1;
    msgs[0].buf = &reg_addr;
    
    /* Read message: read the data */
    msgs[1].addr = device->address;
    msgs[1].flags = I2C_M_RD;  /* Read */
    msgs[1].len = length;
    msgs[1].buf = data;
    
    rdwr_data.msgs = msgs;
    rdwr_data.nmsgs = 2;
    
    if (ioctl(device->fd, I2C_RDWR, &rdwr_data) < 0) {
        /* Fallback: try separate write/read operations */
        if (write(device->fd, &reg_addr, 1) != 1) {
            OLOG_ERROR("Error writing register address 0x%02X: %s",
                    reg_addr, strerror(errno));
            return -1;
        }
        
        /* Small delay to ensure device processes the address */
        usleep(1000);  /* 1ms delay */
        
        if (read(device->fd, data, length) != length) {
            OLOG_ERROR("Error reading %d bytes from register 0x%02X: %s",
                    length, reg_addr, strerror(errno));
            return -1;
        }
    }
    
    return 0;
}

/**
 * @brief Read 24-bit register from I2C device
 */
int i2c_read_register24(i2c_device_t *device, uint8_t reg_addr, uint32_t *value)
{
    if (!device || device->fd < 0 || !value) {
        return -1;
    }
    
    uint8_t buf[3];
    
    /* Write register address */
    if (write(device->fd, &reg_addr, 1) != 1) {
        OLOG_ERROR("Error writing register address 0x%02X: %s",
                reg_addr, strerror(errno));
        return -1;
    }
    
    /* Read 3 bytes */
    if (read(device->fd, buf, 3) != 3) {
        OLOG_ERROR("Error reading 24-bit register 0x%02X: %s",
                reg_addr, strerror(errno));
        return -1;
    }
    
    /* Convert from big-endian, 24-bit value */
    *value = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
    
    return 0;
}

/**
 * @brief Sleep for specified milliseconds
 */
void i2c_msleep(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

