/**
 * @file cpu_monitor.c
 * @brief CPU Monitoring Implementation
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
#include <unistd.h>

#include "cpu_monitor.h"
#include "logging.h"

/* Static variables */
static float cpu_usage = 0.0f;
static int cpu_monitor_initialized = 0;

/* Previous CPU state values */
static long double prev_total = 0.0;
static long double prev_idle = 0.0;

/**
 * @brief Initialize CPU monitoring
 * 
 * @return int 0 on success, negative on error
 */
int cpu_monitor_init(void)
{
   FILE *fp;
   long double a[6];
   
   /* Open /proc/stat to read initial CPU values */
   fp = fopen("/proc/stat", "r");
   if (fp == NULL) {
      OLOG_ERROR("Failed to open /proc/stat");
      return -1;
   }
   
   /* Read CPU times - user, nice, system, idle, iowait, irq */
   if (fscanf(fp, "%*s %Lf %Lf %Lf %Lf %Lf %Lf", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6) {
      OLOG_ERROR("Failed to read CPU values from /proc/stat");
      fclose(fp);
      return -1;
   }
   
   fclose(fp);
   
   /* Calculate initial values */
   prev_idle = a[3];
   prev_total = a[0] + a[1] + a[2] + a[3] + a[4] + a[5];
   
   /* Mark as initialized */
   cpu_monitor_initialized = 1;
   
   OLOG_INFO("CPU monitoring initialized");
   return 0;
}

/**
 * @brief Get CPU utilization percentage
 * 
 * This function calculates CPU usage percentage by comparing the
 * current CPU usage times with the previous readings.
 * 
 * @return float CPU utilization percentage (0-100)
 */
float cpu_monitor_get_usage(void)
{
   FILE *fp;
   long double a[6];
   long double total, idle, delta_total, delta_idle;
   
   /* Check if initialized */
   if (!cpu_monitor_initialized) {
      /* Try to initialize */
      if (cpu_monitor_init() != 0) {
         return -1.0f;
      }
   }
   
   /* Open /proc/stat to read current CPU values */
   fp = fopen("/proc/stat", "r");
   if (fp == NULL) {
      OLOG_ERROR("Failed to open /proc/stat");
      return cpu_usage; /* Return last known value */
   }
   
   /* Read CPU times - user, nice, system, idle, iowait, irq */
   if (fscanf(fp, "%*s %Lf %Lf %Lf %Lf %Lf %Lf", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6) {
      OLOG_ERROR("Failed to read CPU values from /proc/stat");
      fclose(fp);
      return cpu_usage; /* Return last known value */
   }
   
   fclose(fp);
   
   /* Calculate current values */
   idle = a[3];
   total = a[0] + a[1] + a[2] + a[3] + a[4] + a[5];
   
   /* Calculate deltas */
   delta_idle = idle - prev_idle;
   delta_total = total - prev_total;
   
   /* Calculate CPU usage percentage */
   if (delta_total > 0.0) {
      cpu_usage = 100.0f * (1.0f - (float)(delta_idle / delta_total));
   }
   
   /* Update previous values for next call */
   prev_idle = idle;
   prev_total = total;
   
   return cpu_usage;
}

/**
 * @brief Clean up CPU monitoring resources
 */
void cpu_monitor_cleanup(void)
{
   cpu_monitor_initialized = 0;
   OLOG_INFO("CPU monitoring cleaned up");
}

