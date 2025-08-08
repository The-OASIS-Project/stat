/**
 * @file system_temp_monitor.h
 * @brief System Temperature Monitoring Functions
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

#ifndef SYSTEM_TEMP_MONITOR_H
#define SYSTEM_TEMP_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize system temperature monitoring
 * 
 * Scans thermal zones to find junction temperature sensor (tj-thermal)
 * 
 * @return int 0 on success, negative on error
 */
int system_temp_monitor_init(void);

/**
 * @brief Get system temperature in Celsius
 * 
 * @return float System temperature in Celsius or -1.0f if unavailable
 */
float system_temp_monitor_get_temp(void);

/**
 * @brief Clean up system temperature monitoring resources
 */
void system_temp_monitor_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_TEMP_MONITOR_H */
