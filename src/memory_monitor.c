/**
 * @file memory_monitor.c
 * @brief Memory Monitoring Implementation
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory_monitor.h"
#include "logging.h"

/* Static variables */
static float memory_usage = 0.0f;
static int memory_monitor_initialized = 0;

/**
 * @brief Initialize memory monitoring
 * 
 * @return int 0 on success, negative on error
 */
int memory_monitor_init(void)
{
   /* Check if /proc/meminfo is readable */
   FILE *fp = fopen("/proc/meminfo", "r");
   if (fp == NULL) {
      OLOG_ERROR("Failed to open /proc/meminfo");
      return -1;
   }
   
   fclose(fp);
   
   /* Mark as initialized */
   memory_monitor_initialized = 1;
   
   OLOG_INFO("Memory monitoring initialized");
   return 0;
}

/**
 * @brief Get memory utilization percentage
 * 
 * This function calculates memory usage percentage by reading
 * memory information from /proc/meminfo.
 * 
 * @return float Memory utilization percentage (0-100)
 */
float memory_monitor_get_usage(void)
{
   char buf[100];
   char *cp = NULL;
   float mem_total = 0.0f;
   float mem_avail = 0.0f;
   FILE *fp;
   
   /* Check if initialized */
   if (!memory_monitor_initialized) {
      /* Try to initialize */
      if (memory_monitor_init() != 0) {
         return -1.0f;
      }
   }
   
   fp = fopen("/proc/meminfo", "r");
   if (fp == NULL) {
      OLOG_ERROR("Failed to open /proc/meminfo");
      return memory_usage; /* Return last known value */
   }
   
   /* Read MemTotal */
   if (fgets(buf, sizeof(buf), fp) == NULL) {
      OLOG_ERROR("Failed to read MemTotal from /proc/meminfo");
      fclose(fp);
      return memory_usage;
   }
   
   cp = &buf[9]; /* Skip "MemTotal:" */
   mem_total = strtol(cp, NULL, 10);
   
   /* Skip MemFree line */
   if (fgets(buf, sizeof(buf), fp) == NULL) {
      OLOG_ERROR("Failed to read MemFree from /proc/meminfo");
      fclose(fp);
      return memory_usage;
   }
   
   /* Read MemAvailable */
   if (fgets(buf, sizeof(buf), fp) == NULL) {
      OLOG_ERROR("Failed to read MemAvailable from /proc/meminfo");
      fclose(fp);
      return memory_usage;
   }
   
   cp = &buf[13]; /* Skip "MemAvailable:" */
   mem_avail = strtol(cp, NULL, 10);
   
   fclose(fp);
   
   /* Calculate memory usage percentage */
   if (mem_total > 0) {
      memory_usage = ((mem_total - mem_avail) / mem_total) * 100.0f;
   }
   
   return memory_usage;
}

/**
 * @brief Clean up memory monitoring resources
 */
void memory_monitor_cleanup(void)
{
   memory_monitor_initialized = 0;
   OLOG_INFO("Memory monitoring cleaned up");
}

