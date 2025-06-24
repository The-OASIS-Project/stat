/**
 * @file fan_monitor.h
 * @brief Fan Monitoring Functions
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

#ifndef FAN_MONITOR_H
#define FAN_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the fan monitoring subsystem
 * 
 * @return int 0 on success, negative on error
 */
int fan_monitor_init(void);

/**
 * @brief Sets the maximum expected RPM value for the fan
 * 
 * @param max_rpm The maximum RPM value expected
 */
void fan_monitor_set_max_rpm(int max_rpm);

/**
 * @brief Gets the current fan RPM
 * 
 * @return int The current RPM value, or -1 if unavailable
 */
int fan_monitor_get_rpm(void);

/**
 * @brief Gets the fan load as a percentage
 * 
 * @return int Percentage of fan's maximum RPM (0-100), or -1 if unavailable
 */
int fan_monitor_get_load_percent(void);

/**
 * @brief Clean up fan monitoring resources
 */
void fan_monitor_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* FAN_MONITOR_H */

