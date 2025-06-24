/**
 * @file memory_monitor.h
 * @brief Memory Monitoring Functions
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

#ifndef MEMORY_MONITOR_H
#define MEMORY_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize memory monitoring
 * 
 * @return int 0 on success, negative on error
 */
int memory_monitor_init(void);

/**
 * @brief Get memory utilization percentage
 * 
 * @return float Memory utilization percentage (0-100)
 */
float memory_monitor_get_usage(void);

/**
 * @brief Clean up memory monitoring resources
 */
void memory_monitor_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* MEMORY_MONITOR_H */

