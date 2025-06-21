/**
 * @file ina238_registers.h
 * @brief INA238 Register Definitions
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
 * This header contains all register addresses and bit field definitions
 * for the INA238 power monitor chip.
 */

#ifndef INA238_REGISTERS_H
#define INA238_REGISTERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* INA238 Register Addresses */
#define INA238_REG_CONFIG           0x00    ///< Configuration Register
#define INA238_REG_ADC_CONFIG       0x01    ///< ADC Configuration Register
#define INA238_REG_SHUNT_CAL        0x02    ///< Shunt Calibration Register
#define INA238_REG_VSHUNT           0x04    ///< Shunt Voltage Register
#define INA238_REG_VBUS             0x05    ///< Bus Voltage Register
#define INA238_REG_DIETEMP          0x06    ///< Die Temperature Register
#define INA238_REG_CURRENT          0x07    ///< Current Register
#define INA238_REG_POWER            0x08    ///< Power Register
#define INA238_REG_MANUFACTURER_ID  0x3E    ///< Manufacturer ID Register
#define INA238_REG_DEVICE_ID        0x3F    ///< Device ID Register

/* Configuration Register Bits */
#define CONFIG_RANGE_BIT            (1 << 4)    ///< ADC Range bit
#define CONFIG_ADC_RESET_BIT        (1 << 15)   ///< ADC Reset bit

/* ADC Configuration Register - Averaging */
#define ADCCONFIG_AVERAGES_1        (0 << 0)    ///< No averaging
#define ADCCONFIG_AVERAGES_4        (1 << 0)    ///< 4 sample average
#define ADCCONFIG_AVERAGES_16       (2 << 0)    ///< 16 sample average
#define ADCCONFIG_AVERAGES_64       (3 << 0)    ///< 64 sample average
#define ADCCONFIG_AVERAGES_128      (4 << 0)    ///< 128 sample average
#define ADCCONFIG_AVERAGES_256      (5 << 0)    ///< 256 sample average
#define ADCCONFIG_AVERAGES_512      (6 << 0)    ///< 512 sample average
#define ADCCONFIG_AVERAGES_1024     (7 << 0)    ///< 1024 sample average

/* ADC Configuration Register - Temperature Conversion Time */
#define ADCCONFIG_VTCT_50US         (0 << 3)    ///< 50 µs
#define ADCCONFIG_VTCT_84US         (1 << 3)    ///< 84 µs
#define ADCCONFIG_VTCT_150US        (2 << 3)    ///< 150 µs
#define ADCCONFIG_VTCT_280US        (3 << 3)    ///< 280 µs
#define ADCCONFIG_VTCT_540US        (4 << 3)    ///< 540 µs
#define ADCCONFIG_VTCT_1052US       (5 << 3)    ///< 1052 µs
#define ADCCONFIG_VTCT_2074US       (6 << 3)    ///< 2074 µs
#define ADCCONFIG_VTCT_4170US       (7 << 3)    ///< 4170 µs

/* ADC Configuration Register - Shunt Voltage Conversion Time */
#define ADCCONFIG_VSHCT_MASK        (7 << 6)    ///< Shunt voltage conversion time mask
#define ADCCONFIG_VSHCT_50US        (0 << 6)    ///< 50 µs
#define ADCCONFIG_VSHCT_84US        (1 << 6)    ///< 84 µs
#define ADCCONFIG_VSHCT_150US       (2 << 6)    ///< 150 µs
#define ADCCONFIG_VSHCT_280US       (3 << 6)    ///< 280 µs
#define ADCCONFIG_VSHCT_540US       (4 << 6)    ///< 540 µs
#define ADCCONFIG_VSHCT_1052US      (5 << 6)    ///< 1052 µs
#define ADCCONFIG_VSHCT_2074US      (6 << 6)    ///< 2074 µs
#define ADCCONFIG_VSHCT_4170US      (7 << 6)    ///< 4170 µs

/* ADC Configuration Register - Bus Voltage Conversion Time */
#define ADCCONFIG_VBUSCT_MASK       (7 << 9)    ///< Bus voltage conversion time mask
#define ADCCONFIG_VBUSCT_50US       (0 << 9)    ///< 50 µs
#define ADCCONFIG_VBUSCT_84US       (1 << 9)    ///< 84 µs
#define ADCCONFIG_VBUSCT_150US      (2 << 9)    ///< 150 µs
#define ADCCONFIG_VBUSCT_280US      (3 << 9)    ///< 280 µs
#define ADCCONFIG_VBUSCT_540US      (4 << 9)    ///< 540 µs
#define ADCCONFIG_VBUSCT_1052US     (5 << 9)    ///< 1052 µs
#define ADCCONFIG_VBUSCT_2074US     (6 << 9)    ///< 2074 µs
#define ADCCONFIG_VBUSCT_4170US     (7 << 9)    ///< 4170 µs

/* ADC Configuration Register - Operating Mode (Triggered) */
#define ADCCONFIG_MODE_SHUTDOWN_TRIG        (0 << 12)   ///< Shutdown (triggered)
#define ADCCONFIG_MODE_BUS_TRIG             (1 << 12)   ///< Bus voltage (triggered)
#define ADCCONFIG_MODE_SHUNT_TRIG           (2 << 12)   ///< Shunt voltage (triggered)
#define ADCCONFIG_MODE_SHUNT_BUS_TRIG       (3 << 12)   ///< Shunt and bus (triggered)
#define ADCCONFIG_MODE_TEMP_TRIG            (4 << 12)   ///< Temperature (triggered)
#define ADCCONFIG_MODE_TEMP_BUS_TRIG        (5 << 12)   ///< Temperature and bus (triggered)
#define ADCCONFIG_MODE_TEMP_SHUNT_TRIG      (6 << 12)   ///< Temperature and shunt (triggered)
#define ADCCONFIG_MODE_TEMP_SHUNT_BUS_TRIG  (7 << 12)   ///< Temperature, shunt, and bus (triggered)

/* ADC Configuration Register - Operating Mode (Continuous) */
#define ADCCONFIG_MODE_SHUTDOWN_CONT        (8 << 12)   ///< Shutdown (continuous)
#define ADCCONFIG_MODE_BUS_CONT             (9 << 12)   ///< Bus voltage (continuous)
#define ADCCONFIG_MODE_SHUNT_CONT           (10 << 12)  ///< Shunt voltage (continuous)
#define ADCCONFIG_MODE_SHUNT_BUS_CONT       (11 << 12)  ///< Shunt and bus (continuous)
#define ADCCONFIG_MODE_TEMP_CONT            (12 << 12)  ///< Temperature (continuous)
#define ADCCONFIG_MODE_TEMP_BUS_CONT        (13 << 12)  ///< Temperature and bus (continuous)
#define ADCCONFIG_MODE_TEMP_SHUNT_CONT      (14 << 12)  ///< Temperature and shunt (continuous)
#define ADCCONFIG_MODE_TEMP_SHUNT_BUS_CONT  (15 << 12)  ///< Temperature, shunt, and bus (continuous)

/* Device ID Register Masks */
#define INA238_DEVICE_ID_SHIFTS     4                               ///< Device ID bit shift
#define INA238_DEVICE_ID_MASK       (0xFFF << INA238_DEVICE_ID_SHIFTS) ///< Device ID mask
#define INA238_DEVICEID(v)          (((v) & INA238_DEVICE_ID_MASK) >> INA238_DEVICE_ID_SHIFTS) ///< Extract device ID

/* Default ADC Configuration */
#define INA238_DEFAULT_ADC_CONFIG   (ADCCONFIG_MODE_TEMP_SHUNT_BUS_CONT | \
                                    ADCCONFIG_VBUSCT_540US | \
                                    ADCCONFIG_VSHCT_540US | \
                                    ADCCONFIG_VTCT_540US | \
                                    ADCCONFIG_AVERAGES_64)

#ifdef __cplusplus
}
#endif

#endif /* INA238_REGISTERS_H */

