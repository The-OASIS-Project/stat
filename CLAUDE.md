# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Project Overview

STAT (System Telemetry and Analytics Tracker) is the OASIS Project subsystem that
monitors internal hardware conditions and broadcasts live telemetry over MQTT. It
reads power monitors (INA238 single-channel, INA3221 multi-channel), a Daly Smart
BMS (cell-level), CPU/memory load, and thermal/fan status, computes battery state of
charge and runtime estimates from chemistry-specific discharge curves, and publishes
structured JSON for other modules — DAWN (voice interface), MIRAGE (HUD), and the
rest of the suit. It is a small C11 daemon, typically run under systemd on Jetson
(with ARK Electronics carrier auto-detection) or any Linux host with I2C.

See @README.md for the full feature list and usage.

## Critical Rules — Always Follow

- **NEVER delete files.** Tell the developer which files to delete.
- **NEVER run `git add`, `git commit`, or `git push`.** Suggest the command and
  message; let the developer run it.
- **Feedback before implementation.** Provide analysis, trade-offs, and a
  recommendation *first*. Wait for explicit confirmation ("go ahead", "do it",
  "yes") before coding.
- **Format before committing.** Every change must pass `./format_code.sh --check`.
  The pre-commit hook enforces this.
- **Tests must pass before committing.** STAT has a real Unity/ctest suite — run
  `ctest --test-dir build` after any change to battery, Daly BMS, or MQTT JSON code.
- **GPL header on every new `.c`/`.h`.** Template in @CODING_STYLE_GUIDE.md.
- **Design doc commit policy**: commit design docs only when they describe shipped or
  in-flight code. Docs for planned-but-unstarted work stay untracked.

## Build & Test

STAT uses a plain `build/` directory and `CMAKE_BUILD_TYPE` (no CMake presets, unlike
DAWN/MIRAGE). This mirrors what CI does.

```bash
# Configure + build (Debug for development)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
make -C build -j"$(nproc)"

# Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Run
./build/oasis-stat
./build/oasis-stat --list-batteries      # show battery profiles
./build/oasis-stat --battery 4S1P_Samsung50E

# Unit tests (Unity framework, no hardware required)
ctest --test-dir build --output-on-failure
make -C build test_battery_model && ./build/test_battery_model   # single suite

# Format
./format_code.sh                 # fix all
./format_code.sh --changed       # only changed files (fast)
./format_code.sh --check         # CI mode (exit 1 if unformatted)
./format_code.sh --dry-run       # preview
```

- Dependencies: `libmosquitto`, `json-c`, `libm`, I2C support. Install steps in
  @README.md.
- Full/service install: `sudo ./install.sh` (sets up the systemd unit).
- Pre-commit hook: `./install-git-hooks.sh` (one-time).
- Tests run on host (no I2C/serial/broker needed); hardware paths (I2C sensors, Daly
  serial, live MQTT) still need manual testing on device.

## Code Standards

Full standards in @CODING_STYLE_GUIDE.md. Critical gotchas:

- **Logging**: use the `OLOG_*` macros — `OLOG_INFO` / `OLOG_WARNING` / `OLOG_ERROR`
  from `logging.h`. **Not** `LOG_INFO` — the plain `LOG_*` names are avoided because
  they collide with syslog(3). The logging module is byte-identical across DAWN /
  ECHO / MIRAGE / STAT; don't fork it.
- **Safe string copy**: use `safe_strscpy` (fixed arrays) / `safe_strncpy` (pointer +
  size) from `string_utils.h` instead of `strcpy`/`sprintf`/`strcat`. That header is a
  **vendored subset-snapshot** of DAWN's `common/include/utils/string_utils.h` (just
  the copy primitives; uses a neutral `OASIS_STRING_UTILS_H` guard). It's a sync
  point: if DAWN hardens `safe_strncpy`, mirror the change here.
- **Naming**: `snake_case` functions/vars, `UPPER_CASE` constants, `_t` suffix on
  typedefs.
- **Formatting**: 3-space indent, 100-char lines, K&R braces, right-aligned pointers
  (`int *ptr`). Enforced by `.clang-format`.
- **Memory**: prefer static allocation; null-check after malloc; `free(ptr); ptr = NULL;`.
- **Return codes**: the OASIS house standard for *new* code is `SUCCESS` (0) /
  `FAILURE` (1), specific error codes > 1, **never negative**. Note the existing code
  (especially `oasis-stat.c`) predates this and mixes `EXIT_SUCCESS`, bare `0`, and
  `-1` — don't assume it's already compliant. Match the immediate surrounding
  function's convention when patching, and prefer the house standard in new modules.

## MQTT Integration

STAT is a telemetry *publisher*. It broadcasts structured JSON so other OASIS
components can consume it:

- **STAT → DAWN / MIRAGE / others**: power, battery SoC/runtime/health, CPU, memory,
  thermal, fan.
- Default topic `stat/telemetry` (configurable). Messages conform to OASIS conventions
  (ms timestamps, `CLOCK_REALTIME`).

JSON envelopes are built in `mqtt_publisher.c`; `test_mqtt_json.c` verifies envelope
construction without a broker. MQTT auth and TLS are optional (empty username/password
disables auth; TLS off connects unencrypted).

## Hardware & Platform

- **Power monitors**: INA238 (`ina238.c`, single-channel) and INA3221 (`ina3221.c`,
  multi-channel), auto-detected over I2C (`i2c_utils.c`).
- **BMS**: Daly Smart BMS (`daly_bms.c`) — cell-level voltages, deviation/fault
  detection, over serial.
- **Battery model** (`battery_model.c`): non-linear SoC from chemistry-specific
  discharge curves, temperature compensation, runtime estimation. Pack profiles are
  named (e.g. `4S2P_Samsung50E`, `4S1P_Samsung50E`); select via `BATTERY_TYPE` in the
  config or `--battery` on the CLI.
- **ARK Electronics Jetson carrier**: auto-detected (`ark_detection.c`) with optimized
  settings.
- **System metrics**: `cpu_monitor.c`, `memory_monitor.c`, `fan_monitor.c`,
  `system_temp_monitor.c`.

## Configuration Files

- `config/stat.conf` — systemd environment file (sourced by the service): MQTT host/
  port/topic, optional auth + TLS, and `BATTERY_TYPE`. A `--battery` CLI flag
  overrides `BATTERY_TYPE`; an unset/unknown value falls back to the built-in default.
- `config/oasis-stat.service` — systemd unit for service mode.

## File Size Discipline

- **1,500+ lines (C)**: flag as getting large.
- **2,500+ lines**: recommend splitting before adding features.
- **New feature in a large file**: propose a separate module instead.
- **Refactoring large files**: never full rewrites. Incremental extraction — one
  feature at a time, keep original working, run tests after each step.

Current larger files (monitor):

| File | Lines | Notes |
|------|-------|-------|
| `daly_bms.c` | ~1,480 | Approaching threshold — protocol decode + health logic |
| `oasis-stat.c` | ~1,305 | Main daemon / arg parsing / loop; watch for god-module creep |
| `mqtt_publisher.c` | ~1,050 | JSON envelope construction |

## Development Lifecycle

1. **Plan** (non-trivial only) — plan mode + Explore agents for multi-module work.
2. **Implement** — after each logical chunk: `make -C build -j"$(nproc)"` +
   `./format_code.sh --check` + `ctest --test-dir build`.
3. **Review** — run the review agents on the diff (see Code Review Workflow below).
   **Required before every commit of non-trivial work**, not just on request.
4. **Test** — unit tests on host; manual on device for I2C sensors, Daly serial, and
   live MQTT.
5. **Commit** — final `./format_code.sh --check` and green `ctest`; provide the
   `git add` command and a commit message; **developer runs git commands**.

## Code Review Workflow

We review before we commit. Trigger phrases: "code review", "review my changes",
"run the agents", "run the big three", "full review", "what do the agents think?".

1. Capture the diff via `git status` + `git diff` (and read any new untracked files —
   they don't appear in `git diff`).
2. Launch review agents in **parallel**:
   - **Big three** (default): `architecture-reviewer`, `embedded-efficiency-reviewer`,
     `security-auditor`.
   - Add `correctness-reviewer` for logic/bounds/ordering-sensitive changes (parsers,
     state machines, anything with indexing or off-by-one exposure).
   - **Full review**: add `coding-standards-auditor` — for large refactors, new
     modules, or pre-release audits.
   - STAT has no GUI, so `ui-design-architect` is not part of the set (unlike DAWN/
     MIRAGE).
3. For a memory-safety or protocol-parsing fix, validate with an ASan/UBSan build and
   a regression test that is confirmed to trip the sanitizer *without* the fix — a
   green test that never fails on the bug proves nothing.
4. Synthesize findings into a consolidated table with severity and action (fix / skip
   / ask). **Fix pre-existing issues when found** — triage on merit (severity + fix
   effort), not on when introduced.
5. Apply approved fixes; re-verify format + `ctest`.
6. **Re-review substantial post-review changes.** Code written *after* the agent pass
   was seen by no reviewer — re-run the relevant lens on it before commit.

## License

GPLv3 or later. Every new source file includes the GPL header block (see
@CODING_STYLE_GUIDE.md).
