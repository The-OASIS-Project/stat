/**
 * @file system_temp_monitor.c
 * @brief System Temperature Monitoring Implementation
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

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "system_temp_monitor.h"
#include "logging.h"

/* Path for thermal zones */
#define THERMAL_ZONE_PATH "/sys/devices/virtual/thermal/thermal_zone"
#define THERMAL_ZONE_MAX  20  /* Check up to 20 thermal zones */

/* Static variables */
static int system_temp_monitor_initialized = 0;
static int system_temp_zone_index = -1;
static float system_temp = -1.0f;
static char system_temp_path[PATH_MAX] = "";

/**
 * @brief Find the thermal zone that corresponds to the system junction temperature
 * 
 * This function scans all thermal zones and looks for the tj-thermal zone first,
 * then falls back to cpu-thermal if tj-thermal is not found.
 * 
 * @return int Index of the thermal zone or -1 if not found
 */
static int find_system_thermal_zone(void)
{
   char type_path[PATH_MAX];
   char type_buffer[64];
   FILE *type_file;
   int found_tj = -1;
   int found_cpu = -1;
   
   /* Iterate through all thermal zones to find tj-thermal or cpu-thermal */
   for (int i = 0; i < THERMAL_ZONE_MAX; i++) {
      snprintf(type_path, sizeof(type_path), "%s%d/type", THERMAL_ZONE_PATH, i);
      
      /* Try to open the type file */
      type_file = fopen(type_path, "r");
      if (type_file == NULL) {
         continue;  /* Skip if cannot open */
      }
      
      /* Read the type */
      if (fgets(type_buffer, sizeof(type_buffer), type_file) != NULL) {
         /* Remove newline if present */
         size_t len = strlen(type_buffer);
         if (len > 0 && type_buffer[len-1] == '\n') {
            type_buffer[len-1] = '\0';
         }
         
         /* Check if this is tj-thermal (junction temperature) */
         if (strstr(type_buffer, "tj-thermal") != NULL) {
            found_tj = i;
            OLOG_INFO("Found junction thermal zone: %s (zone %d)", type_buffer, i);
            /* Create path to temperature file */
            snprintf(system_temp_path, sizeof(system_temp_path), 
                     "%s%d/temp", THERMAL_ZONE_PATH, i);
            fclose(type_file);
            return found_tj;  /* Return immediately, tj-thermal is preferred */
         }
         
         /* If we haven't found tj-thermal yet, check for cpu-thermal as backup */
         if (found_cpu == -1 && 
             (strstr(type_buffer, "cpu-thermal") != NULL || 
              strstr(type_buffer, "CPU-therm") != NULL)) {
            found_cpu = i;
            OLOG_INFO("Found CPU thermal zone: %s (zone %d)", type_buffer, i);
            /* Save path for cpu-thermal zone in case we don't find tj-thermal */
            snprintf(system_temp_path, sizeof(system_temp_path), 
                     "%s%d/temp", THERMAL_ZONE_PATH, i);
         }
      }
      
      fclose(type_file);
   }
   
   /* If cpu-thermal was found, use it as fallback */
   if (found_cpu != -1) {
      OLOG_INFO("Using CPU thermal zone as fallback for system temperature");
      return found_cpu;
   }
   
   /* If neither was found, log error */
   OLOG_ERROR("Could not find suitable thermal zone for system temperature");
   return -1;
}

/**
 * @brief Initialize system temperature monitoring
 * 
 * @return int 0 on success, -1 on error
 */
int system_temp_monitor_init(void)
{
   /* Check if already initialized */
   if (system_temp_monitor_initialized) {
      return 0;
   }
   
   /* Find system thermal zone */
   system_temp_zone_index = find_system_thermal_zone();
   if (system_temp_zone_index == -1) {
      OLOG_ERROR("System temperature monitoring initialization failed");
      return -1;
   }
   
   /* Set initialized flag before getting temperature to prevent recursion */
   system_temp_monitor_initialized = 1;
   
   /* Get initial temperature */
   system_temp = system_temp_monitor_get_temp();
   OLOG_INFO("System temperature monitoring initialized (zone: %d)", system_temp_zone_index);
   
   return 0;
}

/**
 * @brief Get system temperature in Celsius
 * 
 * @return float System temperature in Celsius or -1.0f if unavailable
 */
float system_temp_monitor_get_temp(void)
{
   FILE *temp_file;
   char temp_buffer[16];
   float temperature = -1.0f;
   
   /* Check if initialized */
   if (!system_temp_monitor_initialized) {
      /* Try to initialize but avoid recursive calls */
      if (system_temp_monitor_init() != 0) {
         return -1.0f;
      }
      return system_temp; /* Return value set during initialization */
   }
   
   /* Open temperature file */
   temp_file = fopen(system_temp_path, "r");
   if (temp_file == NULL) {
      OLOG_ERROR("Failed to open system temperature file: %s", system_temp_path);
      return -1.0f;
   }
   
   /* Read temperature value */
   if (fgets(temp_buffer, sizeof(temp_buffer), temp_file) != NULL) {
      /* Convert to float and divide by 1000 (value is in millidegrees Celsius) */
      temperature = atof(temp_buffer) / 1000.0f;
   } else {
      OLOG_ERROR("Failed to read system temperature");
   }
   
   fclose(temp_file);
   
   /* Update last known temperature if valid */
   if (temperature >= 0.0f) {
      system_temp = temperature;
   } else if (system_temp >= 0.0f) {
      /* Return last known temperature if current read failed */
      temperature = system_temp;
   }
   
   return temperature;
}

/**
 * @brief Clean up system temperature monitoring resources
 */
void system_temp_monitor_cleanup(void)
{
   system_temp_monitor_initialized = 0;
   system_temp_zone_index = -1;
   system_temp = -1.0f;
   OLOG_INFO("System temperature monitoring cleaned up");
}

