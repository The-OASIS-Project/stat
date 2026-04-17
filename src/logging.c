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
 * the project author(s).
 *
 * Unified OASIS logging implementation (canonical — kept byte-identical
 * across DAWN, ECHO, MIRAGE, STAT).
 */

#include "logging.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>

/*
 * Enum values in logging.h are hardcoded to avoid pulling <syslog.h> into
 * every TU.  Verify at compile time that they still match the platform's
 * syslog(3) priority constants.
 */
_Static_assert(LOGLEVEL_INFO == LOG_INFO, "LOGLEVEL_INFO must match syslog LOG_INFO");
_Static_assert(LOGLEVEL_WARNING == LOG_WARNING, "LOGLEVEL_WARNING must match syslog LOG_WARNING");
_Static_assert(LOGLEVEL_ERROR == LOG_ERR, "LOGLEVEL_ERROR must match syslog LOG_ERR");

/* ANSI color codes for console output. */
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_RESET "\x1b[0m"

/* Fixed preamble width.  Sized to fit typical "[LEVEL] HH:MM:SS.mmm
 * filename.c:NNNN: " with filenames up to ~25 chars. */
#define PREAMBLE_WIDTH 55

/*
 * File-scope state.  log_file is written only at init/shutdown (documented
 * contract: no concurrent access).  The toggles are _Atomic so they can be
 * flipped safely from any thread (e.g., TUI suppressing console output).
 */
static FILE *log_file = NULL;
static atomic_int use_syslog = 0;
static atomic_int suppress_console = 0;

/* Strip leading directory components from a path. */
static const char *get_filename(const char *path) {
   const char *filename = strrchr(path, '/');
   if (!filename) {
      filename = strrchr(path, '\\');
   }
   return filename ? filename + 1 : path;
}

/* Format current wall-clock time as "HH:MM:SS.mmm" (thread-safe). */
static void get_timestamp_ms(char *buffer, size_t buffer_size) {
   struct timeval tv;
   struct tm tm_storage;

   gettimeofday(&tv, NULL);
   localtime_r(&tv.tv_sec, &tm_storage);

   snprintf(buffer, buffer_size, "%02d:%02d:%02d.%03d", tm_storage.tm_hour, tm_storage.tm_min,
            tm_storage.tm_sec, (int)(tv.tv_usec / 1000));
}

/* Remove newline and carriage return characters in place.  Fast path when
 * the message has none (the common case). */
static void remove_newlines(char *str) {
   if (!strpbrk(str, "\r\n")) {
      return;
   }
   char *src = str, *dst = str;
   while (*src) {
      if (*src != '\n' && *src != '\r') {
         *dst++ = *src;
      }
      src++;
   }
   *dst = '\0';
}

void log_message_v(log_level_t level, const char *file, int line, const char *fmt, va_list args) {
   /* Syslog path — no timestamp (syslog adds its own), no colors. */
   if (atomic_load_explicit(&use_syslog, memory_order_relaxed)) {
      char msg[MAX_LOG_LENGTH];
      vsnprintf(msg, sizeof(msg), fmt, args);
      remove_newlines(msg);
      syslog(level, "[%s:%d] %s", get_filename(file), line, msg);
      return;
   }

   /* If console is suppressed and no log file is open, discard. */
   if (atomic_load_explicit(&suppress_console, memory_order_relaxed) && !log_file) {
      return;
   }

   const char *level_str = NULL;
   const char *color_code = NULL;
   FILE *output_stream = log_file ? log_file : stdout;

   switch (level) {
      case LOGLEVEL_INFO:
         level_str = "INFO";
         color_code = ANSI_COLOR_GREEN;
         break;
      case LOGLEVEL_WARNING:
         level_str = "WARN";
         color_code = ANSI_COLOR_YELLOW;
         break;
      case LOGLEVEL_ERROR:
         level_str = "ERR ";
         color_code = ANSI_COLOR_RED;
         output_stream = log_file ? log_file : stderr;
         break;
      default:
         return;
   }

   const char *filename = get_filename(file);

   char timestamp[13]; /* "HH:MM:SS.mmm" + NUL */
   get_timestamp_ms(timestamp, sizeof(timestamp));

   /* +2 allows snprintf's NUL past the max-truncation point. */
   char preamble[PREAMBLE_WIDTH + 2];
   int preamble_length = snprintf(preamble, sizeof(preamble), "[%s] %s %s:%d: ", level_str,
                                  timestamp, filename, line);

   if (preamble_length < 0) {
      return;
   }

   if (preamble_length > PREAMBLE_WIDTH) {
      preamble[PREAMBLE_WIDTH] = '\0';
   } else {
      int padding_length = PREAMBLE_WIDTH - preamble_length;
      memset(preamble + preamble_length, ' ', padding_length);
      preamble[PREAMBLE_WIDTH] = '\0';
   }

   /* Format into a stack buffer so we can strip newlines (log-injection
    * defense) before writing.  Single fprintf per line keeps output atomic
    * across threads (per-FILE lock inside libc). */
   char msg[MAX_LOG_LENGTH];
   vsnprintf(msg, sizeof(msg), fmt, args);
   remove_newlines(msg);

   if (log_file) {
      fprintf(output_stream, "%s%s\n", preamble, msg);
   } else {
      fprintf(output_stream, "%s%s%s%s\n", color_code, preamble, msg, ANSI_COLOR_RESET);
   }
}

void log_message(log_level_t level, const char *file, int line, const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   log_message_v(level, file, line, fmt, args);
   va_end(args);
}

int init_logging(const char *filename, int mode) {
   /* Validate arguments before touching state — avoids closing an existing
    * log file for a subsequently-rejected request. */
   if (mode == LOG_TO_FILE && !filename) {
      fprintf(stderr, "init_logging: filename cannot be NULL with LOG_TO_FILE\n");
      return 1;
   }

   /* Close any previously opened log file. */
   if (log_file) {
      fclose(log_file);
      log_file = NULL;
   }

   /* Clear syslog mode if a previous init_syslog() was active. */
   if (atomic_load_explicit(&use_syslog, memory_order_relaxed)) {
      closelog();
      atomic_store_explicit(&use_syslog, 0, memory_order_relaxed);
   }

   if (mode == LOG_TO_FILE) {
      log_file = fopen(filename, "w");
      if (!log_file) {
         fprintf(stderr, "init_logging: failed to open log file: %s\n", filename);
         return 1;
      }
      /* Restrict permissions — log contents may include session metadata
       * that should not be world-readable. */
      if (fchmod(fileno(log_file), 0600) != 0) {
         fprintf(stderr, "init_logging: warning: fchmod(0600) failed on %s\n", filename);
      }
   }

   return 0;
}

int init_syslog(const char *ident) {
   if (log_file) {
      fclose(log_file);
      log_file = NULL;
   }
   openlog(ident, LOG_PID, LOG_DAEMON);
   atomic_store_explicit(&use_syslog, 1, memory_order_relaxed);
   return 0;
}

void close_logging(void) {
   if (log_file) {
      fclose(log_file);
      log_file = NULL;
   }
   if (atomic_load_explicit(&use_syslog, memory_order_relaxed)) {
      closelog();
      atomic_store_explicit(&use_syslog, 0, memory_order_relaxed);
   }
}

void logging_suppress_console(int suppress) {
   atomic_store_explicit(&suppress_console, suppress, memory_order_relaxed);
}
