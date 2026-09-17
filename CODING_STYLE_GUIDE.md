# C Coding Style Guide
## OASIS STAT Code Style Standards

### Philosophy
This style guide balances professional best practices with hacker pragmatism. Code
should be readable, maintainable, and elegant—but never at the expense of progress.
When in doubt, favor clarity and function over formalism. It matches the shared OASIS
house style used across DAWN, MIRAGE, and ECHO, so code reads the same across the
suit.

---

## 1. Indentation & Spacing

### Indentation
- **Use 3 spaces** for indentation (no tabs)
- Rationale: good visual hierarchy without excessive horizontal space

```c
void example_function(void) {
   if (condition) {
      do_something();
      if (nested_condition) {
         do_nested_thing();
      }
   }
}
```

### Spacing Around Operators
- Space around binary operators: `a + b`, `x == y`, `ptr->field`
- No space for unary operators: `!flag`, `*ptr`, `&variable`
- Space after commas and after keywords: `if (`, `for (`, `while (`
- Two spaces before trailing comments

### Pointer Alignment
- **Pointers align right** (to the variable name): `int *ptr`, `const char *str`
- Enforced by clang-format

### Line Length
- **100 characters maximum** (enforced by clang-format)
- Break long lines at logical points

---

## 2. Braces & Control Structures

### Brace Style (K&R)
- Opening brace on the same line for functions, conditionals, loops
- Closing brace on its own line
- **Always use braces**, even for single-statement blocks (enforced by clang-format)
- **No single-line control structures** (`if (x) do_thing();` is expanded by the
  formatter)

```c
int my_function(int param) {
   if (condition) {
      single_statement();
   } else {
      other_statement();
   }

   for (int i = 0; i < count; i++) {
      process(i);
   }
}
```

### Switch Statements
- Indent case labels one level
- Always include `break` or a `/* fall through */` comment

---

## 3. Naming Conventions

- **Functions / variables**: `snake_case`, descriptive
  (`calculate_battery_voltage()`, not `calc()`). Short loop counters (`i`, `j`) OK.
- **Static functions**: prefix with module name or keep private.
- **Constants / macros**: `UPPER_CASE_WITH_UNDERSCORES`. Prefer enums over `#define`
  for related constants.
- **Types**: typedef with `_t` suffix, `snake_case` name.

```c
typedef struct battery_config_t {
   int capacity_mah;
   float voltage_nominal;
} battery_config_t;

typedef enum {
   STATE_IDLE,
   STATE_ACTIVE,
   STATE_ERROR
} state_t;
```

---

## 4. Comments & Documentation

### File Headers
Every source/header file starts with the GPL license block, then a brief description.

```c
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
 * [Brief description of file purpose]
 */
```

### Function Documentation
- Doxygen-style comments for public APIs; minimal comments for obvious private helpers.
- Comment the **why**, not the **what**.

```c
/**
 * @brief Initializes the battery monitoring system.
 *
 * @param config Pointer to battery configuration structure.
 * @return SUCCESS on success, an error code (> 1) on failure.
 */
int init_battery_monitor(battery_config_t *config);
```

### TODO Markers
`// TODO:`, `// FIXME:`, `// HACK:`, `// NOTE:`.

---

## 5. Header Files

- **Header guards**: `#ifndef <FILENAME>_H` / `#define` / `#endif`.
- **Include order** (auto-sorted by clang-format, blank line between groups):
  1. System C headers (`<stdio.h>`)
  2. Library headers (`<mosquitto.h>`, `<json-c/json.h>`)
  3. Local project headers (`"logging.h"`)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>
#include <mosquitto.h>

#include "battery_model.h"
#include "logging.h"
```

- One `.h` per `.c`; internal-only declarations go in a `*_internal.h` (see
  `daly_bms_internal.h`, `mqtt_publisher_internal.h`).

---

## 6. Function Design

- **Length**: soft target < 50 lines. No hard limit — clarity over line counts.
- **Parameter order**: inputs first, outputs last; context/state parameters first.

```c
int read_sensor(i2c_bus_t *bus, uint8_t address, float *result);
```

---

## 7. Return Values & Error Handling

### House standard (for new code)
- **Use `SUCCESS` (0) and `FAILURE` (1)** for status returns.
- Define specific error codes **> 1** for detailed reporting.
- **Do NOT use negative return values** (no `-1`, no negative errno).
- For count/size functions, return status and pass the count via an output parameter.

```c
#define SUCCESS 0
#define FAILURE 1
#define ERR_INVALID_PARAM 2
#define ERR_TIMEOUT       3

int configure_sensor(const sensor_config_t *config) {
   if (!config) {
      return ERR_INVALID_PARAM;
   }
   if (!wait_for_ready(1000)) {
      return ERR_TIMEOUT;
   }
   return SUCCESS;
}
```

> **Reality check for STAT**: parts of the existing codebase (notably `oasis-stat.c`)
> predate this convention and mix `EXIT_SUCCESS`, bare `0`, and `-1`. When patching an
> existing function, match its local convention for consistency; when writing a new
> module, follow the house standard above. Don't assume existing return values are
> already compliant.

### Checking returns
Always check returns from functions that can fail, and log at the appropriate level.

```c
int ret = init_sensor();
if (ret != SUCCESS) {
   OLOG_ERROR("Failed to initialize sensor: error code %d", ret);
   return FAILURE;
}
```

### Cleanup
Prefer direct cleanup calls. Use `goto` sparingly for complex multi-resource cleanup,
with descriptive labels freeing in reverse order.

---

## 8. Memory Management

- **Prefer static allocation** for this embedded-ish daemon; use fixed-size stack
  buffers where practical.
- On dynamic allocation: null-check, initialize, and `free(ptr); ptr = NULL;`.
- **String safety**: bounded functions only.

```c
char *buffer = malloc(size);
if (!buffer) {
   OLOG_ERROR("Memory allocation failed");
   return FAILURE;
}
memset(buffer, 0, size);
/* ... */
free(buffer);
buffer = NULL;

strncpy(dest, src, sizeof(dest) - 1);
dest[sizeof(dest) - 1] = '\0';
snprintf(buffer, sizeof(buffer), "Value: %d", value);
```

---

## 9. Project-Specific Conventions

### Logging — use `OLOG_*`, not `LOG_*`
STAT uses the unified OASIS logging module (`logging.h`), byte-identical across DAWN /
ECHO / MIRAGE / STAT. The canonical macros carry an **`O` prefix** to avoid colliding
with syslog(3)'s `LOG_INFO` / `LOG_WARNING` / `LOG_ERR`.

```c
#include "logging.h"

OLOG_INFO("System initialized");
OLOG_WARNING("Battery voltage low: %.2fV", voltage);
OLOG_ERROR("I2C communication failed: %d", error);
```

- Format strings must be compile-time literals.
- `LOG_CREDENTIAL_STATUS(key)` yields `"(configured)"` / `"(not configured)"` — use it
  instead of logging secret values.

### Threading
Document thread safety; protect shared state with a mutex and say so.

```c
/* Thread-safe: protected by config_mutex */
static config_t global_config;
static pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;
```

### MQTT
STAT publishes telemetry JSON (built in `mqtt_publisher.c`). Keep envelope changes
covered by `tests/test_mqtt_json.c`.

### Configuration
Prefer runtime config over compile-time; validate values; provide sensible defaults
(e.g. the `BATTERY_TYPE` fallback).

---

## 10. Testing

STAT ships a real Unity/ctest suite (`tests/`, vendored Unity under `tests/unity/`).
All suites run on the host with **no hardware, serial, or broker**:

| Suite | Covers |
|-------|--------|
| `test_battery_model` | Discharge curves, chemistry/SoC parsing |
| `test_daly_parsing`  | Daly frame decoders + checksum |
| `test_daly_health`   | Cell deviation + fault severity |
| `test_mqtt_json`     | MQTT JSON envelope construction |

Run them with `ctest --test-dir build --output-on-failure`. Add or extend a suite when
you touch battery math, Daly parsing/health, or MQTT JSON. CI runs format-check, then
build + ctest.

---

## 11. Anti-Patterns

**Avoid**: magic numbers, deeply nested logic, inconsistent error handling, unmatched
malloc/free, ignored return values, `LOG_*` (use `OLOG_*`).

**Do**: fail fast and loudly, log at the right level, write testable code, document
assumptions, keep it simple.

---

## Appendix: clang-format

`.clang-format` (identical across the OASIS repos) enforces indentation (3 spaces),
100-char lines, K&R braces, right-aligned pointers, include grouping/sorting, and
brace-on-every-block. It does **not** enforce naming, function length, comment quality,
error-handling patterns, or memory practices — those need review.

```bash
./format_code.sh              # format all (required before commit)
./format_code.sh --changed    # only uncommitted/staged files (fast)
./format_code.sh --dry-run    # preview
./format_code.sh --check      # CI mode (exit 1 if unformatted)
```

Key settings: `BasedOnStyle: LLVM`, `IndentWidth: 3`, `ColumnLimit: 100`,
`PointerAlignment: Right`, `BreakBeforeBraces: Custom` (K&R), `SortIncludes: true`.

---

**Philosophy**: living document. When the guide gets in the way of shipping, update the
guide—not the code. Consistency within a module matters more than consistency with the
guide.
