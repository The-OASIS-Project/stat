/*
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

#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <syslog.h>

// Log levels - same values as syslog
typedef enum {
    LOGLEVEL_INFO = LOG_INFO,
    LOGLEVEL_WARNING = LOG_WARNING,
    LOGLEVEL_ERROR = LOG_ERR,
} log_level_t;

#ifdef __cplusplus
extern "C" {
#endif

// Logging function
void log_message(log_level_t level, const char *file, int line, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

// Initialization function
int init_logging(const char *filename, int to_file);
int init_syslog(const char *ident);

// Close logging function
void close_logging(void);

// Logging modes
#define LOG_TO_CONSOLE 0
#define LOG_TO_FILE 1
#define LOG_TO_SYSLOG 2

// Macros for easy logging
#define OLOG_INFO(fmt, ...) log_message(LOGLEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define OLOG_WARNING(fmt, ...) log_message(LOGLEVEL_WARNING, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define OLOG_ERROR(fmt, ...) log_message(LOGLEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif // LOGGING_H

