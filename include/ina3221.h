/**
 * @file ina3221.h
 * @brief INA3221 3-Channel Power Monitor Driver Header (sysfs interface)
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
 * This header defines the interface for the INA3221 3-channel power monitor
 * driver using the Linux hwmon sysfs interface.
 */

#ifndef INA3221_H
#define INA3221_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* INA3221 Constants */
#define INA3221_MAX_CHANNELS     3
#define INA3221_LABEL_MAX_LEN    32
#define INA3221_PATH_MAX_LEN     256

/* Default sysfs search paths */
#define INA3221_SYSFS_BASE       "/sys/bus/i2c/drivers/ina3221"
#define INA3221_HWMON_PATTERN    "hwmon/hwmon*"

/**
 * @brief INA3221 channel data structure
 */
typedef struct {
   int channel;                        ///< Channel number (1, 2, or 3)
   float voltage;                      ///< Bus voltage in Volts
   float current;                      ///< Current in Amps
   float power;                        ///< Power in Watts (calculated)
   char label[INA3221_LABEL_MAX_LEN];  ///< Channel label/name
   float shunt_resistor;               ///< Shunt resistor value in Ohms
   bool enabled;                       ///< Channel enabled status
   bool valid;                         ///< Data validity flag
} ina3221_channel_t;

/**
 * @brief INA3221 device structure
 */
typedef struct {
   char sysfs_path[INA3221_PATH_MAX_LEN];  ///< Base sysfs path to hwmon
   ina3221_channel_t channels[INA3221_MAX_CHANNELS]; ///< Channel data
   int num_active_channels;             ///< Number of enabled channels
   bool initialized;                    ///< Initialization status
   char device_name[64];                ///< Device name
} ina3221_device_t;

/**
 * @brief Combined measurements from all channels
 */
typedef struct {
   ina3221_channel_t channels[INA3221_MAX_CHANNELS];
   int num_channels;
   bool valid;                  ///< Overall validity
} ina3221_measurements_t;

/* Function Prototypes */

/**
 * @brief Initialize the INA3221 device using sysfs interface
 * 
 * @param dev Pointer to device structure
 * @return int 0 on success, negative on error
 */
int ina3221_init(ina3221_device_t *dev);

/**
 * @brief Close the INA3221 device
 * 
 * @param dev Pointer to device structure
 */
void ina3221_close(ina3221_device_t *dev);

/**
 * @brief Read measurements from all enabled channels
 * 
 * @param dev Pointer to device structure
 * @param measurements Pointer to measurements structure to fill
 * @return int 0 on success, negative on error
 */
int ina3221_read_measurements(ina3221_device_t *dev, ina3221_measurements_t *measurements);

/**
 * @brief Read measurements from a specific channel
 * 
 * @param dev Pointer to device structure
 * @param channel Channel number (1, 2, or 3)
 * @param channel_data Pointer to channel data structure to fill
 * @return int 0 on success, negative on error
 */
int ina3221_read_channel(ina3221_device_t *dev, int channel, ina3221_channel_t *channel_data);

/**
 * @brief Get the number of active/enabled channels
 * 
 * @param dev Pointer to device structure
 * @return int Number of active channels, negative on error
 */
int ina3221_get_active_channels(ina3221_device_t *dev);

/**
 * @brief Print device status and configuration
 * 
 * @param dev Pointer to device structure
 */
void ina3221_print_status(const ina3221_device_t *dev);

/**
 * @brief Auto-detect INA3221 device in sysfs
 * 
 * @param sysfs_path Buffer to store the detected path
 * @param path_size Size of the buffer
 * @return int 0 on success, negative if not found
 */
int ina3221_detect_device(char *sysfs_path, size_t path_size);

#ifdef __cplusplus
}
#endif

#endif /* INA3221_H */

