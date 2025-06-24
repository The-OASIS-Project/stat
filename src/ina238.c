/**
 * @file ina238.c
 * @brief INA238 Power Monitor Driver Implementation
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
 * This file implements the INA238 power monitor driver functionality.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "ina238.h"
#include "ina238_registers.h"
#include "i2c_utils.h"
#include "logging.h"

/* Private function prototypes */
static int ina238_probe(ina238_device_t *dev);
static int ina238_reset_device(ina238_device_t *dev);
static int ina238_configure_device(ina238_device_t *dev);

/**
 * @brief Probe INA238 device to verify it's present and responding
 */
static int ina238_probe(ina238_device_t *dev)
{
    i2c_device_t i2c_dev = {
        .fd = dev->fd,
        .address = dev->i2c_addr,
        .bus = NULL
    };
    
    uint16_t value;
    
    /* Verify manufacturer ID */
    if (i2c_read_register16(&i2c_dev, INA238_REG_MANUFACTURER_ID, &value) < 0) {
        OLOG_ERROR("Failed to read manufacturer ID");
        return -1;
    }
    
    if (value != INA238_MFG_ID_TI) {
        OLOG_ERROR("Invalid manufacturer ID: 0x%04X (expected 0x%04X)",
                value, INA238_MFG_ID_TI);
        return -1;
    }
    
    /* Verify device ID */
    if (i2c_read_register16(&i2c_dev, INA238_REG_DEVICE_ID, &value) < 0) {
        OLOG_ERROR("Failed to read device ID");
        return -1;
    }
    
    uint16_t device_id = INA238_DEVICEID(value);
    if (device_id != INA238_MFG_DIE) {
        OLOG_ERROR("Invalid device ID: 0x%04X (expected 0x%04X)",
                device_id, INA238_MFG_DIE);
        return -1;
    }
    
    return 0;
}

/**
 * @brief Reset INA238 device to default state
 */
static int ina238_reset_device(ina238_device_t *dev)
{
    i2c_device_t i2c_dev = {
        .fd = dev->fd,
        .address = dev->i2c_addr,
        .bus = NULL
    };
    
    /* Perform ADC reset */
    if (i2c_write_register16(&i2c_dev, INA238_REG_CONFIG, CONFIG_ADC_RESET_BIT) < 0) {
        OLOG_ERROR("Failed to reset device");
        return -1;
    }
    
    /* Wait for reset to complete */
    i2c_msleep(10);
    
    return 0;
}

/**
 * @brief Configure INA238 device with calculated parameters
 */
static int ina238_configure_device(ina238_device_t *dev)
{
    i2c_device_t i2c_dev = {
        .fd = dev->fd,
        .address = dev->i2c_addr,
        .bus = NULL
    };
    
    /* Set shunt calibration */
    if (i2c_write_register16(&i2c_dev, INA238_REG_SHUNT_CAL, dev->shunt_calibration) < 0) {
        OLOG_ERROR("Failed to set shunt calibration");
        return -1;
    }
    
    /* Set CONFIG register with range setting */
    if (i2c_write_register16(&i2c_dev, INA238_REG_CONFIG, dev->range) < 0) {
        OLOG_ERROR("Failed to set CONFIG register");
        return -1;
    }
    
    /* Configure ADC for continuous mode operation */
    if (i2c_write_register16(&i2c_dev, INA238_REG_ADC_CONFIG, INA238_DEFAULT_ADC_CONFIG) < 0) {
        OLOG_ERROR("Failed to set ADC configuration");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Initialize INA238 device
 */
int ina238_init(ina238_device_t *dev, const char *i2c_bus, uint8_t i2c_addr, 
                float r_shunt, float max_current)
{
    i2c_device_t i2c_dev;
    
    /* Clear device structure */
    memset(dev, 0, sizeof(ina238_device_t));
    
    /* Store configuration parameters */
    dev->i2c_addr = i2c_addr;
    dev->max_current = max_current;
    dev->rshunt = r_shunt;
    
    /* Calculate derived parameters */
    dev->current_lsb = dev->max_current / INA238_DN_MAX;
    dev->power_lsb = dev->current_lsb * POWER_LSB_MULTIPLIER;
    
    /* Determine ADC range based on maximum current */
    dev->range = (dev->max_current > (DEFAULT_MAX_CURRENT - 1.0f)) ? 
                  INA238_ADCRANGE_HIGH : INA238_ADCRANGE_LOW;
    
    /* Calculate shunt calibration */
    dev->shunt_calibration = (uint16_t)(INA238_CONST * dev->current_lsb * dev->rshunt);
    if (dev->range == INA238_ADCRANGE_LOW) {
        dev->shunt_calibration *= 4;
    }
    
    /* Open I2C device */
    if (i2c_open_device(&i2c_dev, i2c_bus, i2c_addr) < 0) {
        OLOG_ERROR("Failed to open I2C device %s at address 0x%02X",
                i2c_bus, i2c_addr);
        return -1;
    }
    
    /* Store file descriptor */
    dev->fd = i2c_dev.fd;
    
    /* Probe device */
    if (ina238_probe(dev) < 0) {
        i2c_close_device(&i2c_dev);
        return -1;
    }
    
    /* Reset device */
    if (ina238_reset_device(dev) < 0) {
        i2c_close_device(&i2c_dev);
        return -1;
    }
    
    /* Configure device */
    if (ina238_configure_device(dev) < 0) {
        i2c_close_device(&i2c_dev);
        return -1;
    }
    
    dev->initialized = true;
    
    return 0;
}

/**
 * @brief Close INA238 device
 */
void ina238_close(ina238_device_t *dev)
{
    if (dev && dev->fd >= 0) {
        i2c_device_t i2c_dev = {
            .fd = dev->fd,
            .address = dev->i2c_addr,
            .bus = NULL
        };
        i2c_close_device(&i2c_dev);
        dev->fd = -1;
        dev->initialized = false;
    }
}

/**
 * @brief Read all measurements from INA238
 */
int ina238_read_measurements(ina238_device_t *dev, ina238_measurements_t *measurements)
{
    if (!dev || !dev->initialized || !measurements) {
        return -1;
    }
    
    /* Clear measurements structure */
    memset(measurements, 0, sizeof(ina238_measurements_t));
    
    /* Read individual measurements */
    measurements->bus_voltage = ina238_read_bus_voltage(dev);
    measurements->current = ina238_read_current(dev);
    measurements->power = ina238_read_power(dev);
    measurements->temperature = ina238_read_temperature(dev);
    
    /* Mark as valid if we got reasonable values */
    measurements->valid = (measurements->bus_voltage != 0.0f || 
                          measurements->current != 0.0f || 
                          measurements->power != 0.0f);
    
    return measurements->valid ? 0 : -1;
}

/**
 * @brief Read bus voltage from INA238
 */
float ina238_read_bus_voltage(ina238_device_t *dev)
{
    if (!dev || !dev->initialized) {
        return 0.0f;
    }
    
    i2c_device_t i2c_dev = {
        .fd = dev->fd,
        .address = dev->i2c_addr,
        .bus = NULL
    };
    
    uint16_t raw_value;
    if (i2c_read_register16(&i2c_dev, INA238_REG_VBUS, &raw_value) < 0) {
        return 0.0f;
    }
    
    return (float)((int16_t)raw_value) * INA238_VSCALE;
}

/**
 * @brief Read current from INA238
 */
float ina238_read_current(ina238_device_t *dev)
{
    if (!dev || !dev->initialized) {
        return 0.0f;
    }
    
    i2c_device_t i2c_dev = {
        .fd = dev->fd,
        .address = dev->i2c_addr,
        .bus = NULL
    };
    
    uint16_t raw_value;
    if (i2c_read_register16(&i2c_dev, INA238_REG_CURRENT, &raw_value) < 0) {
        return 0.0f;
    }
    
    return (float)((int16_t)raw_value) * dev->current_lsb;
}

/**
 * @brief Read power from INA238
 */
float ina238_read_power(ina238_device_t *dev)
{
    if (!dev || !dev->initialized) {
        return 0.0f;
    }
    
    i2c_device_t i2c_dev = {
        .fd = dev->fd,
        .address = dev->i2c_addr,
        .bus = NULL
    };
    
    uint32_t raw_value;
    if (i2c_read_register24(&i2c_dev, INA238_REG_POWER, &raw_value) < 0) {
        return 0.0f;
    }
    
    return (float)raw_value * dev->power_lsb;
}

/**
 * @brief Read die temperature from INA238
 */
float ina238_read_temperature(ina238_device_t *dev)
{
    if (!dev || !dev->initialized) {
        return 0.0f;
    }
    
    i2c_device_t i2c_dev = {
        .fd = dev->fd,
        .address = dev->i2c_addr,
        .bus = NULL
    };
    
    uint16_t raw_value;
    if (i2c_read_register16(&i2c_dev, INA238_REG_DIETEMP, &raw_value) < 0) {
        return 0.0f;
    }
    
    return (float)((int16_t)raw_value) * INA238_TSCALE;
}

/**
 * @brief Print device status and configuration
 */
void ina238_print_status(const ina238_device_t *dev)
{
    if (!dev) {
        printf("INA238 device: NULL\n");
        return;
    }
    
    printf("INA238 Device Status:\n");
    printf("  Initialized: %s\n", dev->initialized ? "Yes" : "No");
    
    if (dev->initialized) {
        printf("  I2C Address: 0x%02X\n", dev->i2c_addr);
        printf("  Max Current: %.2f A\n", dev->max_current);
        printf("  Shunt Resistance: %.6f Ω\n", dev->rshunt);
        printf("  Current LSB: %.9f A/bit\n", dev->current_lsb);
        printf("  Power LSB: %.9f W/bit\n", dev->power_lsb);
        printf("  Shunt Calibration: 0x%04X\n", dev->shunt_calibration);
        printf("  ADC Range: %s\n", (dev->range == INA238_ADCRANGE_HIGH) ? 
               "HIGH (±163.84mV)" : "LOW (±40.96mV)");
    }
    printf("\n");
}

