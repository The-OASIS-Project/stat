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
 * Unified OASIS logging interface (canonical — kept byte-identical across
 * DAWN, ECHO, MIRAGE, STAT).
 *
 * Usage contract:
 *   - Call init_logging() or init_syslog() once at startup, before any
 *     thread that logs is spawned.
 *   - Call close_logging() once at shutdown, after all logging threads
 *     have joined.
 *   - Format strings passed to OLOG_* must be compile-time literals; only
 *     arguments may be user-controlled data (passed via %s).
 *   - log_message() is NOT async-signal-safe: do not call from signal
 *     handlers.
 */

#ifndef OASIS_LOGGING_H
#define OASIS_LOGGING_H

#include <stdarg.h>
#include <stdio.h>

/*
 * Log levels.  Numeric values match syslog(3) priorities so syslog dispatch
 * is a zero-cost pass-through.  <syslog.h> is NOT pulled in here — its
 * ~25 LOG_* macros would pollute every TU that includes this header.  A
 * _Static_assert in logging.c verifies these values match syslog at
 * compile time.
 */
typedef enum {
   LOGLEVEL_ERROR = 3,   /* matches syslog LOG_ERR */
   LOGLEVEL_WARNING = 4, /* matches syslog LOG_WARNING */
   LOGLEVEL_INFO = 6,    /* matches syslog LOG_INFO */
} log_level_t;

/* Logging modes for init_logging(). */
#define LOG_TO_CONSOLE 0
#define LOG_TO_FILE 1
#define LOG_TO_SYSLOG 2 /* Reserved constant; use init_syslog() to enter this mode. */

/* Maximum formatted message length (bytes, including NUL). */
#define MAX_LOG_LENGTH 1024

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Core logging function.  Called via the OLOG_* macros; direct use is
 *        permitted but rare.
 *
 * @param level Severity (LOGLEVEL_INFO / LOGLEVEL_WARNING / LOGLEVEL_ERROR).
 * @param file  Source filename (usually __FILE__).
 * @param line  Source line number (usually __LINE__).
 * @param fmt   printf-style format string (must be a literal).
 * @param ...   Format arguments.
 */
void log_message(log_level_t level, const char *file, int line, const char *fmt, ...);

/**
 * @brief va_list variant of log_message().  Used by callers that already
 *        have a va_list (e.g., callback bridges) to avoid a redundant
 *        vsnprintf.
 */
void log_message_v(log_level_t level, const char *file, int line, const char *fmt, va_list args);

/**
 * @brief Initialise console or file logging.  Call once at startup.
 *
 * @param filename Log file path.  Ignored when mode == LOG_TO_CONSOLE.
 * @param mode     LOG_TO_CONSOLE or LOG_TO_FILE.
 * @return         0 on success, non-zero on failure.
 */
int init_logging(const char *filename, int mode);

/**
 * @brief Switch logging output to syslog.  Call once at startup.
 *
 * @param ident Syslog identifier (typically the daemon name).  Must remain
 *              valid for the lifetime of the process (stored by syslog).
 * @return      0 on success.
 */
int init_syslog(const char *ident);

/**
 * @brief Close any open log file and/or syslog connection.  Call once at
 *        shutdown, after all logging threads have joined.
 */
void close_logging(void);

/**
 * @brief Suppress console output (for TUI or headless modes).
 *
 * Safe to call from any thread after init; value is read atomically in
 * log_message().
 *
 * @param suppress 1 to suppress, 0 to restore.
 */
void logging_suppress_console(int suppress);

#ifdef __cplusplus
}
#endif

/*
 * Convenience macros.  OLOG_* is the canonical namespace; the "O" prefix
 * avoids collision with syslog(3)'s LOG_INFO / LOG_WARNING / LOG_ERR
 * preprocessor constants on TUs that include <syslog.h> directly.
 */
#define OLOG_INFO(fmt, ...) log_message(LOGLEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define OLOG_WARNING(fmt, ...) log_message(LOGLEVEL_WARNING, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define OLOG_ERROR(fmt, ...) log_message(LOGLEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/*
 * Debug logging — compiled to a no-op in release builds.  Message level is
 * INFO; intent is developer-only tracing that should not ship.
 */
#ifdef DEBUG
#define OLOG_DEBUG(fmt, ...) log_message(LOGLEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define OLOG_DEBUG(fmt, ...) \
   do {                      \
   } while (0)
#endif

/*
 * Credential-safe status helper.  Prevents accidental logging of the raw
 * credential value by substituting a status string.  Use wherever API keys,
 * OAuth tokens, or passwords would otherwise end up in a format string.
 */
#define LOG_CREDENTIAL_STATUS(key) ((key) ? "(configured)" : "(not configured)")

#endif /* OASIS_LOGGING_H */
