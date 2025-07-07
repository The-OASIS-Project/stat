/**
 * @file ina3221.c
 * @brief INA3221 3-Channel Power Monitor Driver Implementation (sysfs interface)
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
 * This file implements the INA3221 power monitor driver functionality
 * using the Linux hwmon sysfs interface.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <glob.h>

#include "ina3221.h"
#include "logging.h"

/* Private function prototypes */
static int ina3221_read_sysfs_file(const char *path, char *buffer, size_t buffer_size);
static int ina3221_read_sysfs_int(const char *path, int *value);
static int ina3221_init_channel(ina3221_device_t *dev, int channel);
static int ina3221_find_hwmon_path(const char *base_path, char *hwmon_path, size_t path_size);

/**
 * @brief Read a string value from a sysfs file
 */
static int ina3221_read_sysfs_file(const char *path, char *buffer, size_t buffer_size)
{
   FILE *fp = fopen(path, "r");
   if (!fp) {
      return -1;
   }
   
   if (fgets(buffer, buffer_size, fp) == NULL) {
      fclose(fp);
      return -1;
   }
   
   fclose(fp);
   
   /* Remove trailing newline */
   size_t len = strlen(buffer);
   if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
   }
   
   return 0;
}

/**
 * @brief Read an integer value from a sysfs file
 */
static int ina3221_read_sysfs_int(const char *path, int *value)
{
   char buffer[64];
   if (ina3221_read_sysfs_file(path, buffer, sizeof(buffer)) < 0) {
      return -1;
   }
   
   *value = atoi(buffer);
   return 0;
}

/**
 * @brief Find the hwmon path for INA3221 device
 */
static int ina3221_find_hwmon_path(const char *base_path, char *hwmon_path, size_t path_size)
{
   char pattern[1024];  /* Increased buffer size */
   glob_t glob_result;
   int ret = -1;
   
   /* Look for hwmon/hwmon* pattern */
   int len = snprintf(pattern, sizeof(pattern), "%s/hwmon/hwmon*", base_path);
   if (len >= (int)sizeof(pattern)) {
      OLOG_ERROR("Pattern buffer too small for path: %s", base_path);
      return -1;
   }
   
   if (glob(pattern, GLOB_NOSORT, NULL, &glob_result) == 0) {
      if (glob_result.gl_pathc > 0) {
         /* Use the first match */
         size_t copy_len = strlen(glob_result.gl_pathv[0]);
         if (copy_len < path_size) {
            strcpy(hwmon_path, glob_result.gl_pathv[0]);
            ret = 0;
         } else {
            OLOG_ERROR("hwmon path too long: %s", glob_result.gl_pathv[0]);
         }
      }
   }
   
   globfree(&glob_result);
   return ret;
}

/**
 * @brief Auto-detect INA3221 device in sysfs
 */
int ina3221_detect_device(char *sysfs_path, size_t path_size)
{
   DIR *dir;
   struct dirent *entry;
   char device_path[1024];   /* Increased buffer size */
   char hwmon_path[1024];    /* Increased buffer size */
   char name_path[1024];     /* Increased buffer size */
   char name_buffer[64];
   
   /* Open the INA3221 driver directory */
   dir = opendir(INA3221_SYSFS_BASE);
   if (!dir) {
      OLOG_ERROR("Cannot open INA3221 sysfs directory: %s", INA3221_SYSFS_BASE);
      return -1;
   }
   
   /* Look for device directories (e.g., 1-0040) */
   while ((entry = readdir(dir)) != NULL) {
      if (entry->d_name[0] == '.') {
         continue;
      }
      
      /* Build device path */
      int len = snprintf(device_path, sizeof(device_path), "%s/%s", INA3221_SYSFS_BASE, entry->d_name);
      if (len >= (int)sizeof(device_path)) {
         OLOG_WARNING("Device path too long, skipping: %s", entry->d_name);
         continue;
      }
      
      /* Check if this device has hwmon interface */
      if (ina3221_find_hwmon_path(device_path, hwmon_path, sizeof(hwmon_path)) == 0) {
         /* Verify it's actually an INA3221 by checking the name */
         len = snprintf(name_path, sizeof(name_path), "%s/name", hwmon_path);
         if (len >= (int)sizeof(name_path)) {
            OLOG_WARNING("Name path too long, skipping");
            continue;
         }
         
         if (ina3221_read_sysfs_file(name_path, name_buffer, sizeof(name_buffer)) == 0) {
            if (strstr(name_buffer, "ina3221") != NULL) {
               /* Found it! */
               size_t copy_len = strlen(hwmon_path);
               if (copy_len < path_size) {
                  strcpy(sysfs_path, hwmon_path);
                  closedir(dir);
                  return 0;
               } else {
                  OLOG_ERROR("sysfs path too long: %s", hwmon_path);
               }
            }
         }
      }
   }
   
   closedir(dir);
   OLOG_ERROR("INA3221 device not found in sysfs");
   return -1;
}

/**
 * @brief Initialize a specific channel
 */
static int ina3221_init_channel(ina3221_device_t *dev, int channel)
{
   char path[1024];          /* Increased buffer size */
   char label_buffer[64];
   int enabled = 0;
   int shunt_resistor_uohm = 0;
   
   if (channel < 1 || channel > INA3221_MAX_CHANNELS) {
      return -1;
   }
   
   ina3221_channel_t *ch = &dev->channels[channel - 1];
   ch->channel = channel;
   ch->valid = false;
   
   /* Check if channel is enabled */
   int len = snprintf(path, sizeof(path), "%s/in%d_enable", dev->sysfs_path, channel);
   if (len >= (int)sizeof(path)) {
      OLOG_ERROR("Enable path too long for channel %d", channel);
      return -1;
   }
   
   if (ina3221_read_sysfs_int(path, &enabled) == 0) {
      ch->enabled = (enabled != 0);
   } else {
      /* If enable file doesn't exist, assume enabled */
      ch->enabled = true;
   }
   
   if (!ch->enabled) {
      OLOG_INFO("INA3221 Channel %d is disabled", channel);
      return 0;
   }
   
   /* Read channel label */
   len = snprintf(path, sizeof(path), "%s/in%d_label", dev->sysfs_path, channel);
   if (len >= (int)sizeof(path)) {
      OLOG_ERROR("Label path too long for channel %d", channel);
      return -1;
   }
   
   if (ina3221_read_sysfs_file(path, label_buffer, sizeof(label_buffer)) == 0) {
      /* Use safer string copy */
      size_t copy_len = strlen(label_buffer);
      if (copy_len >= INA3221_LABEL_MAX_LEN) {
         copy_len = INA3221_LABEL_MAX_LEN - 1;
      }
      memcpy(ch->label, label_buffer, copy_len);
      ch->label[copy_len] = '\0';
   } else {
      snprintf(ch->label, INA3221_LABEL_MAX_LEN, "Channel %d", channel);
   }
   
   /* Read shunt resistor value (in microohms) */
   len = snprintf(path, sizeof(path), "%s/shunt%d_resistor", dev->sysfs_path, channel);
   if (len >= (int)sizeof(path)) {
      OLOG_ERROR("Shunt path too long for channel %d", channel);
      return -1;
   }
   
   if (ina3221_read_sysfs_int(path, &shunt_resistor_uohm) == 0) {
      ch->shunt_resistor = (float)shunt_resistor_uohm / 1000000.0f; /* Convert µΩ to Ω */
   } else {
      ch->shunt_resistor = 0.001f; /* Default 1mΩ */
   }
   
   OLOG_INFO("INA3221 Channel %d (%s): Enabled, Shunt=%.6f Ω", 
            channel, ch->label, ch->shunt_resistor);
   
   return 0;
}

/**
 * @brief Initialize the INA3221 device using sysfs interface
 */
int ina3221_init(ina3221_device_t *dev)
{
   char name_path[1024];     /* Increased buffer size */
   char device_name[64];
   
   if (!dev) {
      return -1;
   }
   
   /* Clear device structure */
   memset(dev, 0, sizeof(ina3221_device_t));
   
   /* Auto-detect device */
   if (ina3221_detect_device(dev->sysfs_path, sizeof(dev->sysfs_path)) < 0) {
      return -1;
   }
   
   /* Read device name */
   int len = snprintf(name_path, sizeof(name_path), "%s/name", dev->sysfs_path);
   if (len >= (int)sizeof(name_path)) {
      OLOG_ERROR("Name path too long");
      return -1;
   }
   
   if (ina3221_read_sysfs_file(name_path, device_name, sizeof(device_name)) == 0) {
      size_t copy_len = strlen(device_name);
      if (copy_len >= sizeof(dev->device_name)) {
         copy_len = sizeof(dev->device_name) - 1;
      }
      memcpy(dev->device_name, device_name, copy_len);
      dev->device_name[copy_len] = '\0';
   } else {
      strcpy(dev->device_name, "ina3221");
   }
   
   /* Initialize all channels */
   dev->num_active_channels = 0;
   for (int i = 1; i <= INA3221_MAX_CHANNELS; i++) {
      if (ina3221_init_channel(dev, i) == 0 && dev->channels[i-1].enabled) {
         dev->num_active_channels++;
      }
   }
   
   if (dev->num_active_channels == 0) {
      OLOG_ERROR("No active channels found on INA3221");
      return -1;
   }
   
   dev->initialized = true;
   OLOG_INFO("INA3221 initialized: %d active channels at %s", 
            dev->num_active_channels, dev->sysfs_path);
   
   return 0;
}

/**
 * @brief Close the INA3221 device
 */
void ina3221_close(ina3221_device_t *dev)
{
   if (dev) {
      dev->initialized = false;
      dev->num_active_channels = 0;
      memset(dev->sysfs_path, 0, sizeof(dev->sysfs_path));
   }
}

/**
 * @brief Read measurements from a specific channel
 */
int ina3221_read_channel(ina3221_device_t *dev, int channel, ina3221_channel_t *channel_data)
{
   char path[1024];          /* Increased buffer size */
   int voltage_mv, current_ma;
   
   if (!dev || !dev->initialized || channel < 1 || channel > INA3221_MAX_CHANNELS || !channel_data) {
      return -1;
   }
   
   ina3221_channel_t *ch = &dev->channels[channel - 1];
   
   if (!ch->enabled) {
      channel_data->valid = false;
      return -1;
   }
   
   /* Copy static channel info */
   *channel_data = *ch;
   channel_data->valid = false;
   
   /* Read bus voltage (in mV) */
   int len = snprintf(path, sizeof(path), "%s/in%d_input", dev->sysfs_path, channel);
   if (len >= (int)sizeof(path)) {
      OLOG_ERROR("Voltage path too long for channel %d", channel);
      return -1;
   }
   
   if (ina3221_read_sysfs_int(path, &voltage_mv) < 0) {
      OLOG_ERROR("Failed to read voltage for channel %d", channel);
      return -1;
   }
   channel_data->voltage = (float)voltage_mv / 1000.0f; /* Convert mV to V */
   
   /* Read current (in mA) */
   len = snprintf(path, sizeof(path), "%s/curr%d_input", dev->sysfs_path, channel);
   if (len >= (int)sizeof(path)) {
      OLOG_ERROR("Current path too long for channel %d", channel);
      return -1;
   }
   
   if (ina3221_read_sysfs_int(path, &current_ma) < 0) {
      OLOG_ERROR("Failed to read current for channel %d", channel);
      return -1;
   }
   channel_data->current = (float)current_ma / 1000.0f; /* Convert mA to A */
   
   /* Calculate power */
   channel_data->power = channel_data->voltage * channel_data->current;
   
   channel_data->valid = true;
   return 0;
}

/**
 * @brief Read measurements from all enabled channels (simplified)
 */
int ina3221_read_measurements(ina3221_device_t *dev, ina3221_measurements_t *measurements)
{
   if (!dev || !dev->initialized || !measurements) {
      return -1;
   }

   /* Clear measurements structure */
   memset(measurements, 0, sizeof(ina3221_measurements_t));

   measurements->num_channels = 0;
   measurements->valid = false;

   /* Read each enabled channel */
   for (int i = 1; i <= INA3221_MAX_CHANNELS; i++) {
      if (dev->channels[i-1].enabled) {
         ina3221_channel_t *ch = &measurements->channels[measurements->num_channels];

         if (ina3221_read_channel(dev, i, ch) == 0) {
            measurements->num_channels++;
            measurements->valid = true;
         }
      }
   }

   return measurements->valid ? 0 : -1;
}

/**
 * @brief Get the number of active/enabled channels
 */
int ina3221_get_active_channels(ina3221_device_t *dev)
{
   if (!dev || !dev->initialized) {
      return -1;
   }
   
   return dev->num_active_channels;
}

/**
 * @brief Print device status and configuration
 */
void ina3221_print_status(const ina3221_device_t *dev)
{
   if (!dev) {
      printf("INA3221 device: NULL\n");
      return;
   }
   
   printf("INA3221 Device Status:\n");
   printf("  Initialized: %s\n", dev->initialized ? "Yes" : "No");
   
   if (dev->initialized) {
      printf("  Device Name: %s\n", dev->device_name);
      printf("  Sysfs Path: %s\n", dev->sysfs_path);
      printf("  Active Channels: %d\n", dev->num_active_channels);
      
      for (int i = 0; i < INA3221_MAX_CHANNELS; i++) {
         const ina3221_channel_t *ch = &dev->channels[i];
         printf("  Channel %d (%s): %s, Shunt=%.6f Ω\n",
                ch->channel, ch->label,
                ch->enabled ? "Enabled" : "Disabled",
                ch->shunt_resistor);
      }
   }
   printf("\n");
}

