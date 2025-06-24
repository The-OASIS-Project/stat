/**
 * @file cpu_monitor.h
 * @brief CPU Monitoring Functions
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

#ifndef CPU_MONITOR_H
#define CPU_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize CPU monitoring
 * 
 * @return int 0 on success, negative on error
 */
int cpu_monitor_init(void);

/**
 * @brief Get CPU utilization percentage
 * 
 * @return float CPU utilization percentage (0-100)
 */
float cpu_monitor_get_usage(void);

/**
 * @brief Clean up CPU monitoring resources
 */
void cpu_monitor_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* CPU_MONITOR_H */

