/**
 * @file fan_monitor.c
 * @brief Fan Monitoring Implementation
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

#include "fan_monitor.h"
#include "logging.h"

/* Default max RPM value */
#define FAN_DEFAULT_MAX_RPM 6000

/* Default maximum PWM value */
#define FAN_MAX_PWM 255

/* Static variables */
static char fan_rpm_path[PATH_MAX*2] = "";
static char fan_pwm_path[PATH_MAX*4] = "";
static int fan_monitor_initialized = 0;
static int fan_max_rpm = FAN_DEFAULT_MAX_RPM;
static FILE *rpm_file = NULL;
static FILE *pwm_file = NULL;

/**
 * @brief Finds the fan RPM file on Linux systems, with specific support for Jetson
 *
 * @param rpm_path Buffer to store the RPM file path
 * @param path_size Size of the buffer
 * @return int 0 on success, -1 if not found
 */
static int find_fan_rpm_file(char *rpm_path, size_t path_size)
{
   DIR *dir;
   struct dirent *entry;
   char path[PATH_MAX];
   char test_path[PATH_MAX+11];
   int found = 0;

   /* 1. Try the Jetson tachometer path first (most specific) */
   dir = opendir("/sys/devices/platform");
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         if (strstr(entry->d_name, "bus@0") || strstr(entry->d_name, "tachometer")) {
            snprintf(path, sizeof(path), "/sys/devices/platform/%s", entry->d_name);
            DIR *sub_dir = opendir(path);
            if (sub_dir) {
               struct dirent *sub_entry;
               while ((sub_entry = readdir(sub_dir)) != NULL) {
                  if (strstr(sub_entry->d_name, "tachometer")) {
                     /* Found tachometer directory, now look for hwmon */
                     snprintf(path, sizeof(path), "/sys/devices/platform/%s/%s/hwmon",
                              entry->d_name, sub_entry->d_name);
                     DIR *hwmon_dir = opendir(path);
                     if (hwmon_dir) {
                        struct dirent *hwmon_entry;
                        while ((hwmon_entry = readdir(hwmon_dir)) != NULL) {
                           if (strstr(hwmon_entry->d_name, "hwmon")) {
                              snprintf(rpm_path, path_size, "%s/%s/rpm",
                                       path, hwmon_entry->d_name);
                              if (access(rpm_path, R_OK) == 0) {
                                 found = 1;
                                 OLOG_INFO("Found tachometer RPM file: %s", rpm_path);

                                 /* For Jetson, specifically check for PWM in the pwm-fan device */
                                 /* Clear previous path */
                                 fan_pwm_path[0] = '\0';

                                 /* Check common pwm-fan locations */
                                 for (int i = 0; i < 6; i++) {
                                    snprintf(fan_pwm_path, sizeof(fan_pwm_path),
                                            "/sys/devices/platform/pwm-fan/hwmon/hwmon%d/pwm1", i);
                                    if (access(fan_pwm_path, R_OK) == 0) {
                                       OLOG_INFO("Found PWM file for Jetson: %s", fan_pwm_path);
                                       break;
                                    }
                                    fan_pwm_path[0] = '\0';
                                 }

                                 break;
                              }
                           }
                        }
                        closedir(hwmon_dir);
                        if (found) break;
                     }
                  }
               }
               closedir(sub_dir);
               if (found) break;
            }
         }
      }
      closedir(dir);
      if (found) return 0;
   }

   /* 2. Try the hwmon class (generic approach) */
   dir = opendir("/sys/class/hwmon");
   if (dir) {
      while ((entry = readdir(dir)) != NULL) {
         if (entry->d_name[0] == '.') continue;

         /* Check for direct rpm file */
         snprintf(test_path, sizeof(test_path), "/sys/class/hwmon/%s/rpm", entry->d_name);
         if (access(test_path, R_OK) == 0) {
            strncpy(rpm_path, test_path, path_size);
            rpm_path[path_size - 1] = '\0';
            OLOG_INFO("Found RPM file in hwmon: %s", rpm_path);

            /* Look for PWM file in the same directory */
            char dir_path[PATH_MAX];
            snprintf(dir_path, sizeof(dir_path), "/sys/class/hwmon/%s", entry->d_name);
            for (int i = 1; i <= 5; i++) {
               snprintf(fan_pwm_path, sizeof(fan_pwm_path), "%s/pwm%d", dir_path, i);
               if (access(fan_pwm_path, R_OK) == 0) {
                  OLOG_INFO("Found PWM file: %s", fan_pwm_path);
                  break;
               }
               fan_pwm_path[0] = '\0'; /* Clear if not found */
            }

            return 0;
         }

         /* Check for fan input files */
         for (int i = 1; i <= 5; i++) {
            snprintf(test_path, sizeof(test_path), "/sys/class/hwmon/%s/fan%d_input",
                     entry->d_name, i);
            if (access(test_path, R_OK) == 0) {
               strncpy(rpm_path, test_path, path_size);
               rpm_path[path_size - 1] = '\0';
               OLOG_INFO("Found fan input file in hwmon: %s", rpm_path);

               /* Look for PWM file in the same directory */
               char dir_path[PATH_MAX];
               snprintf(dir_path, sizeof(dir_path), "/sys/class/hwmon/%s", entry->d_name);
               snprintf(fan_pwm_path, sizeof(fan_pwm_path), "%s/pwm%d", dir_path, i);
               if (access(fan_pwm_path, R_OK) == 0) {
                  OLOG_INFO("Found PWM file: %s", fan_pwm_path);
               } else {
                  /* Try other PWM numbers */
                  for (int j = 1; j <= 5; j++) {
                     snprintf(fan_pwm_path, sizeof(fan_pwm_path), "%s/pwm%d", dir_path, j);
                     if (access(fan_pwm_path, R_OK) == 0) {
                        OLOG_INFO("Found PWM file: %s", fan_pwm_path);
                        break;
                     }
                     fan_pwm_path[0] = '\0'; /* Clear if not found */
                  }
               }

               return 0;
            }
         }

         /* Check for device subdirectory */
         snprintf(path, sizeof(path), "/sys/class/hwmon/%s/device", entry->d_name);
         DIR *device_dir = opendir(path);
         if (device_dir) {
            /* Check for rpm in device subdir */
            snprintf(test_path, sizeof(test_path), "%s/rpm", path);
            if (access(test_path, R_OK) == 0) {
               strncpy(rpm_path, test_path, path_size);
               rpm_path[path_size - 1] = '\0';
               OLOG_INFO("Found RPM file in hwmon device: %s", rpm_path);

               /* Look for PWM file in the same directory */
               for (int i = 1; i <= 5; i++) {
                  snprintf(fan_pwm_path, sizeof(fan_pwm_path), "%s/pwm%d", path, i);
                  if (access(fan_pwm_path, R_OK) == 0) {
                     OLOG_INFO("Found PWM file: %s", fan_pwm_path);
                     break;
                  }
                  fan_pwm_path[0] = '\0'; /* Clear if not found */
               }

               closedir(device_dir);
               return 0;
            }

            /* Check for fan inputs in device subdir */
            for (int i = 1; i <= 5; i++) {
               snprintf(test_path, sizeof(test_path), "%s/fan%d_input", path, i);
               if (access(test_path, R_OK) == 0) {
                  strncpy(rpm_path, test_path, path_size);
                  rpm_path[path_size - 1] = '\0';
                  OLOG_INFO("Found fan input in hwmon device: %s", rpm_path);

                  /* Look for PWM file in the same directory */
                  snprintf(fan_pwm_path, sizeof(fan_pwm_path), "%s/pwm%d", path, i);
                  if (access(fan_pwm_path, R_OK) == 0) {
                     OLOG_INFO("Found PWM file: %s", fan_pwm_path);
                  } else {
                     /* Try other PWM numbers */
                     for (int j = 1; j <= 5; j++) {
                        snprintf(fan_pwm_path, sizeof(fan_pwm_path), "%s/pwm%d", path, j);
                        if (access(fan_pwm_path, R_OK) == 0) {
                           OLOG_INFO("Found PWM file: %s", fan_pwm_path);
                           break;
                        }
                        fan_pwm_path[0] = '\0'; /* Clear if not found */
                     }
                  }

                  closedir(device_dir);
                  return 0;
               }
            }
            closedir(device_dir);
         }
      }
      closedir(dir);
   }

   /* 3. Try direct paths for common locations (fallback) */
   const char *common_paths[] = {
      "/sys/devices/platform/pwm-fan/hwmon/hwmon0/rpm",
      "/sys/devices/platform/pwm-fan/hwmon/hwmon1/rpm",
      "/sys/devices/platform/pwm-fan/hwmon/hwmon2/rpm",
      "/sys/devices/platform/pwm-fan/hwmon/hwmon3/rpm",
      "/sys/devices/platform/pwm-fan/hwmon/hwmon4/rpm",
      "/sys/devices/platform/pwm-fan/hwmon/hwmon5/rpm",
      NULL
   };

   for (int i = 0; common_paths[i] != NULL; i++) {
      if (access(common_paths[i], R_OK) == 0) {
         strncpy(rpm_path, common_paths[i], path_size);
         rpm_path[path_size - 1] = '\0';
         OLOG_INFO("Found RPM file at common path: %s", rpm_path);

         /* Look for PWM file in the same directory */
         char dir_path[PATH_MAX];
         strncpy(dir_path, common_paths[i], sizeof(dir_path) - 1);
         dir_path[sizeof(dir_path) - 1] = '\0';

         char *last_slash = strrchr(dir_path, '/');
         if (last_slash) {
            *last_slash = '\0';

            /* Try different PWM file names */
            for (int j = 1; j <= 5; j++) {
               snprintf(fan_pwm_path, sizeof(fan_pwm_path), "%s/pwm%d", dir_path, j);
               if (access(fan_pwm_path, R_OK) == 0) {
                  OLOG_INFO("Found PWM file: %s", fan_pwm_path);
                  break;
               }
               fan_pwm_path[0] = '\0'; /* Clear if not found */
            }
         }

         return 0;
      }
   }

   OLOG_WARNING("Could not find fan RPM file");
   return -1; /* Not found */
}

/**
 * @brief Initialize the fan monitoring subsystem
 * 
 * @return int 0 on success, -1 on failure
 */
int fan_monitor_init(void)
{
   if (fan_monitor_initialized && rpm_file != NULL) {
      return 0; /* Already initialized with valid file */
   }

   /* Close previous file if open */
   if (rpm_file != NULL) {
      fclose(rpm_file);
      rpm_file = NULL;
   }

   if (pwm_file != NULL) {
      fclose(pwm_file);
      pwm_file = NULL;
   }

   if (find_fan_rpm_file(fan_rpm_path, sizeof(fan_rpm_path)) != 0) {
      OLOG_WARNING("Failed to find fan RPM file, fan monitoring disabled");
      return -1;
   }

   /* Open the file for persistent use */
   rpm_file = fopen(fan_rpm_path, "r");
   if (rpm_file == NULL) {
      OLOG_ERROR("Failed to open fan RPM file: %s", fan_rpm_path);
      return -1;
   }

   /* Open PWM file if available */
   if (fan_pwm_path[0] != '\0') {
      pwm_file = fopen(fan_pwm_path, "r");
      if (pwm_file == NULL) {
         OLOG_WARNING("Failed to open fan PWM file: %s, using default max RPM", fan_pwm_path);
      } else {
         OLOG_INFO("Fan PWM file opened: %s", fan_pwm_path);
      }
   }

   OLOG_INFO("Fan monitoring initialized with RPM file: %s", fan_rpm_path);
   fan_monitor_initialized = 1;
   return 0;
}

/**
 * @brief Sets the maximum expected RPM value for the fan
 * 
 * @param max_rpm The maximum RPM value expected
 */
void fan_monitor_set_max_rpm(int max_rpm)
{
   if (max_rpm > 0) {
      fan_max_rpm = max_rpm;
      OLOG_INFO("Fan max RPM set to %d", fan_max_rpm);
   }
}

/**
 * @brief Gets the current fan RPM
 * 
 * @return int The current RPM value, or -1 if unavailable
 */
int fan_monitor_get_rpm(void)
{
   int rpm = -1;
   
   if (!fan_monitor_initialized || rpm_file == NULL) {
      if (fan_monitor_init() != 0) {
         return -1;
      }
   }
   
   /* Reset error indicators */
   clearerr(rpm_file);
   
   /* Go to the beginning of the file */
   rewind(rpm_file);
   
   /* Read the RPM value */
   if (fscanf(rpm_file, "%d", &rpm) != 1) {
      OLOG_WARNING("Failed to read fan RPM value, attempting to reinitialize");
      
      /* Try to reinitialize */
      fan_monitor_initialized = 0;
      fclose(rpm_file);
      rpm_file = NULL;
      
      if (fan_monitor_init() == 0) {
         /* Try again with new file */
         rewind(rpm_file);
         if (fscanf(rpm_file, "%d", &rpm) != 1) {
            rpm = -1;
         }
      }
   }
   
   return rpm;
}

/**
 * @brief Gets the current PWM value of the fan
 *
 * @return int The current PWM value (0-255), or -1 if unavailable
 */
int fan_monitor_get_pwm(void)
{
   int pwm = -1;

   if (!fan_monitor_initialized || pwm_file == NULL) {
      return -1; /* PWM file not available */
   }

   /* Reset error indicators */
   clearerr(pwm_file);

   /* Go to the beginning of the file */
   rewind(pwm_file);

   /* Read the PWM value */
   if (fscanf(pwm_file, "%d", &pwm) != 1) {
      OLOG_WARNING("Failed to read fan PWM value");
      return -1;
   }

   /* Ensure PWM value is in range 0-255 */
   if (pwm < 0) pwm = 0;
   if (pwm > FAN_MAX_PWM) pwm = FAN_MAX_PWM;

   return pwm;
}

/**
 * @brief Gets the fan load as a percentage
 * 
 * @return int Percentage of fan's maximum RPM (0-100), or -1 if unavailable
 */
int fan_monitor_get_load_percent(void)
{
   int rpm = fan_monitor_get_rpm();
   if (rpm < 0) return -1;

   /* Try to use PWM value for percentage if available */
   int pwm = fan_monitor_get_pwm();
   if (pwm >= 0) {
      /* Calculate percentage based on PWM value (0-255) */
      int percent = (pwm * 100) / FAN_MAX_PWM;

      return percent;
   }
   OLOG_WARNING("Falling back to RPM-based...");

   /* Fall back to RPM-based percentage if PWM not available */
   int percent = (rpm * 100) / fan_max_rpm;
   if (percent > 100) percent = 100; /* Cap at 100% */

   return percent;
}

/**
 * @brief Clean up fan monitoring resources
 */
void fan_monitor_cleanup(void)
{
   if (rpm_file != NULL) {
      fclose(rpm_file);
      rpm_file = NULL;
   }

   if (pwm_file != NULL) {
      fclose(pwm_file);
      pwm_file = NULL;
   }

   fan_monitor_initialized = 0;
   fan_rpm_path[0] = '\0';
   fan_pwm_path[0] = '\0';
   OLOG_INFO("Fan monitoring cleaned up");
}

