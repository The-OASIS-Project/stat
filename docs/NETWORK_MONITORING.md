# STAT Network Monitoring — Design & Implementation Plan (Phase 1)

**Status:** In-flight — P0/P1a/P1b implemented and verified on-device (STAT side).
Reviewed (master-plan-reviewer: GREEN-WITH-CHANGES, folded in). P2 (DAWN consumer)
handed to FRIDAY Lead. Now describes shipped code, so committable per the design-doc
policy.
**Author:** Kris Kersey / Friday
**Date:** 2026-09-17
**Scope:** Add IP/network-layer telemetry to STAT and publish it over MQTT so
DAWN can answer "is the network up?", "what are our IPs?", "which device is our
gateway?", and — honestly bounded — "is the cell link up?"

> All box-specific facts below were verified on the target Jetson (kernel 5.15,
> `oasis` service user uid 999) during plan review, not assumed.

---

## 1. Purpose

STAT is the OASIS telemetry *publisher* — "the diagnostic heartbeat of the suit."
It already reports power, battery, CPU, memory, thermal, and fan. Network
reachability is a first-class internal condition and currently a blind spot.

This adds a `network_monitor` module that enumerates interfaces, resolves default
route(s) for both address families, and probes first-hop reachability, then
publishes a `Network` telemetry envelope. **STAT publishes; it does not answer.**
DAWN/MIRAGE turn the telemetry into answers.

### Questions this enables (answered downstream by DAWN)

| User question | Field(s) STAT publishes |
|---|---|
| "What are our IPs?" | per-interface `ipv4` / `ipv6` (stable addresses; privacy/deprecated filtered) |
| "Is the network up?" | per-interface `up`/`carrier` + `reachability[].reachable` + `fail_streak` |
| "Which device is our gateway?" | `default_routes[]` (gateway, iface, metric, family) — DAWN picks primary by **min metric** |
| "Is the cell link up?" | cellular-`kind` iface `up`+`carrier`, its default route, modem gateway answers ICMP (**USB link to modem only — not the bearer**); **bearer proxy** = a global IPv6 (`2607:fb90::`) present on that iface, corroborated by ECHO's registration telemetry |

---

## 2. System boundaries — STAT vs ECHO vs DAWN

Getting this wrong means duplicated telemetry or two daemons fighting one serial
port. The reviewer confirmed the division is clean and complete.

### 2.1 ECHO — the cell *radio* (hands off; STAT touches nothing here)

`ECHO` = *Enhanced Cellular Handling Operations*, driving the Waveshare
**SIM7600G-H** (SIMCom, USB `1e0e:9011`). ECHO **exclusively owns the modem AT
serial port** (if04) and already publishes (`echo/src/mqtt_comms.c:94-100`, ~10s):
`signal_dbm`, `signal_bars`, `registration`, `operator`, `network_type`.

**STAT boundary — hard rules:**
- STAT MUST NOT open any modem AT/serial port (`/dev/ttyUSB*`), issue AT commands,
  or publish signal/registration/operator/call state.
- STAT sees the modem **only** as its network interface (kernel `rndis_host`),
  and **identifies it by `driver`/`kind`, never by the name `usb0`** (the name
  depends on USB enumeration order; `usb1`/`usb2` on this box are Tegra USB-gadget
  ports, not the modem).

"Is the cell connection up?" is answered at two honest altitudes:
- **STAT** → "cellular iface up + carrier, has a default route, modem gateway
  answers, and a global IPv6 bearer address is present" → *link + bearer* up.
- **ECHO** → "3 bars, registered on T-Mobile" → *radio* up.

DAWN fuses them. Neither duplicates the other; neither contends for the port.

*Note (posture):* NetworkManager 1.36 on this box already sends its own
third-party connectivity probes (`connectivity-check.ubuntu.com`), so "STAT sends
no off-host traffic" is a STAT-scoped choice, not a system-wide guarantee.

*Seam for later (ECHO, not STAT):* the true "is the cellular **data bearer**
active" (`+CGACT`/`+CGPADDR`, WAN IP) is owned by nobody today. Recorded in §10
for ECHO to add `bearer_active`; STAT's GUA-v6-present is the interim proxy.

### 2.2 DAWN — the consumer / answerer (downstream; owned by FRIDAY Lead)

Verified seam:
- **`dawn/src/core/stat_service.c:270-277`** — dispatch on message `type`:
  `if "SystemMetrics" … else if "Fan" … else if "BatteryStatus" …`, then a
  comment noting other types are deliberately ignored. **No else-branch, no log →
  a `Network` message is silently dropped today. Forward-compat holds.**
- **`dawn/src/tools/stat_tool.c:151`** — `system_status` GETTER tool, action enum
  `{ all, temps, battery, performance, history, trend }` (6), rendered from a
  cached `stat_snapshot_t`.

**The entire STAT↔DAWN contract is the `type` string + JSON field names** (§5). No
code coupling.

**DAWN-side work (NOT this plan; tracked for FRIDAY Lead), P2:**
1. `stat_service.c`: add `else if (strcmp(type, "Network") == 0)` → parse into new
   `stat_snapshot_t` fields; update the `:277` comment.
2. `stat_service.h` / `stat_service_get_snapshot()`: carry the fields.
3. `stat_tool.c`: add a `network` action, **keyed on `kind`, never the interface
   name**, with an `append_network()` renderer.
4. Contract test in `dawn/tests/test_stat_service.c` against the shared fixture
   `tests/fixtures/network_v1.json` (§8).

Until DAWN adds the branch, the `Network` message is ignored — no flag-day.

`"Battery"` vs `"BatteryStatus"` is **not** a mismatch: STAT emits both (raw
INA238/Daly `Battery`, plus unified `BatteryStatus`); DAWN intentionally consumes
the unified one. **Lesson for us: one `type` per message shape.** If counters ever
ship as a separate message, name it `NetworkCounters` — do not overload `Network`.

### 2.3 MIRAGE — HUD (no work required now)

Subscribes to the same telemetry; may surface network state at its discretion.

---

## 3. Architecture

New self-contained module following STAT's monitor contract, mirroring
`memory_monitor.c` in shape. Reads `/proc`, `/sys`, and sockets; no hardware
coupling; testable on host.

### Files

| File | Change |
|---|---|
| `include/network_monitor.h` | **new** — API + `network_status_t`/`network_config_t` |
| `src/network_monitor.c` | **new** — enumeration, route parse, counters, probe, env config |
| `include/mqtt_publisher_internal.h` | declare `build_network_json()` (test seam) |
| `src/mqtt_publisher.c` | `build_network_json()` + `mqtt_publish_network_data()` |
| `include/mqtt_publisher.h` | declare `mqtt_publish_network_data()` |
| `src/oasis-stat.c` | 3 longopts + init + monotonic-throttled publish + cleanup (~25 lines) |
| `config/stat.conf` | `NET_ENABLE`, `NET_INTERVAL_MS`, `NET_PROBE`, `NET_PROBE_TIMEOUT_MS` |
| `tests/test_network_monitor.c` | **new** — parsers/filter/reply-parse (host-only) |
| `tests/fixtures/network_v1.json` | **new** — canonical payload, shared with DAWN |
| `CMakeLists.txt` | add source + test target |

### Module API — parsers take `FILE *` so tests can drive them via `fmemopen()`

```c
int  network_monitor_init(const network_config_t *cfg);
int  network_monitor_sample(network_status_t *out);        /* caller-owned struct */
void network_monitor_cleanup(void);

int  network_config_from_env(network_config_t *out);        /* keeps oasis-stat.c thin */
/* internal, FILE*-driven for testability: */
int  network_parse_ipv4_routes(FILE *fp, network_route_t *out, size_t cap, size_t *n);
int  network_parse_ipv6_routes(FILE *fp, network_route_t *out, size_t cap, size_t *n);
int  network_parse_inet6_addrs(FILE *fp, /* per-iface v6 addrs, flag-filtered */ ...);
```

Returns house `SUCCESS`(0)/`FAILURE`(1). GPL headers. `safe_strscpy` for iface
names into fixed arrays. `OLOG_*` logging only.

### Memory discipline

- `network_status_t` holds **fixed-cap arrays** (`MAX_IFACES 16`,
  `MAX_ADDRS_PER_IFACE 8`, `MAX_ROUTES 8`). At cap: drop + set a per-array
  `"truncated": true` flag. Aggregate `getifaddrs` entries by iface name **after**
  filtering, so the cap applies to the filtered set.
- `getifaddrs()` list is freed with `freeifaddrs()` **within the same `sample()`**
  — the one libc allocation, does not escape. Documented exception to "prefer
  static."

---

## 4. Data sources (all verified; zero new dependencies)

| Datum | Source | Notes |
|---|---|---|
| iface name, `ifa_flags`, IPv4 | `getifaddrs(3)` | `up = (flags & IFF_UP) && (flags & IFF_RUNNING)` |
| operstate (raw string) | `/sys/class/net/<if>/operstate` | **published raw**; rndis reports `unknown` while routable |
| carrier | `/sys/class/net/<if>/carrier` | `1`/`0` — the real link signal for rndis |
| IPv6 addresses | `/proc/net/if_inet6` | see filtering below — `getifaddrs` can't see temp/deprecated flags |
| IPv4 default route(s) | `/proc/net/route` | text parse; see endianness note |
| IPv6 default route(s) | `/proc/net/ipv6_route` | dest all-zero, plen 00, `flags & RTF_GATEWAY(0x2)` |
| driver / kind | `/sys/class/net/<if>/device/driver` (basename) | rndis_host/qmi_wwan/cdc_mbim → `cellular`; wireless dir → `wifi`; else `ethernet` |
| mtu / speed / mac | `/sys/class/net/<if>/{mtu,speed,address}` | speed wired-only (`-1` elsewhere) |
| rx/tx bytes | `/sys/class/net/<if>/statistics/{rx,tx}_bytes` | **raw cumulative counters** — no rate math in STAT (Kris: include, §7) |
| hostname | `gethostname(2)` | top-level convenience field |

**IPv6 address filtering** (F-M1): `enP8p1s0` currently has 13 v6 addresses (2
stable, 2 current-temporary, 9 temporary+deprecated). Parse `/proc/net/if_inet6`
(cols: addr, ifindex, plen, scope, flags, name). **Default: publish only stable
GUA/ULA** — exclude scope `0x20` (link-local), flags `0x20` (deprecated), and
`0x01` (temporary). Prevents "what are our IPs" answering with dying privacy addrs.

**Interface filter** (F-L3): include iff `/sys/class/net/<if>/device` symlink
exists (physical/USB-backed — excludes `lo`, `docker0`, `br-*`, `veth*`,
`l4tbr0`) **and** `type == 1` (ARPHRD_ETHER — excludes `can0` type 280). Keep
**down** physical NICs (e.g. `wlP1p1s0`) — useful. Deny-list env override retained.

**`/proc/net/route` endianness** (F-parse): gateway/dest hex is the kernel
printing raw `s_addr` with `%08X`. Assign `(in_addr_t)strtoul(hex,NULL,16)`
**directly** to `.s_addr` — no `htonl`, correct on all endianness. Default route:
`Destination==0 && Mask==0 && (Flags & RTF_UP)`; metric is column 7 (0-idx 6).

**Verified live state** (examples must derive from this):
```
enP8p1s0  operstate=up      carrier=1  driver=r8168      speed=1000  IPv4 .159/24; stable v6 present
usb0      operstate=unknown carrier=1  driver=rndis_host mtu=1420    IPv4 225.40/24; T-Mobile GUA v6
wlP1p1s0  operstate=down    driver=rtl88x2ce
IPv4 default: enP8p1s0→192.168.1.1 metric 100 (primary); usb0→192.168.225.1 metric 20100 (failover)
IPv6 default: enP8p1s0→fe80::… metric 100; usb0→fe80::… metric 20100 (both proto ra)
```
NB: NM may **penalize a non-`full` device's metric by +20000** (20100→40100), so
DAWN must select "primary" by **min metric**, never the literals 100/20100.

---

## 5. MQTT contract — the `Network` envelope

`ocp_add_telemetry_envelope(root, "Network")` (`device:"stat"`,
`msg_type:"telemetry"`, `type:"Network"`, ms `timestamp`). `type` is exactly
`"Network"` — DAWN dispatches on it.

```json
{
  "device": "stat", "msg_type": "telemetry", "type": "Network",
  "timestamp": 1750000000000,
  "hostname": "oasis-jetson",
  "interfaces": [
    { "name": "enP8p1s0", "kind": "ethernet", "driver": "r8168",
      "state": "up", "up": true, "carrier": true,
      "mtu": 1500, "speed_mbps": 1000, "mac": "xx:xx:xx:xx:xx:xx",
      "ipv4": ["192.168.1.159"], "ipv6": ["2600:1702:51ae:6ef:...stable..."],
      "rx_bytes": 12345678, "tx_bytes": 2345678 },
    { "name": "usb0", "kind": "cellular", "driver": "rndis_host",
      "state": "unknown", "up": true, "carrier": true,
      "mtu": 1420, "speed_mbps": -1, "mac": "xx:xx:xx:xx:xx:xx",
      "ipv4": ["192.168.225.40"], "ipv6": ["2607:fb90:7c1c:ccc7:...GUA..."],
      "rx_bytes": 987654, "tx_bytes": 456789 }
  ],
  "default_routes": [
    { "iface": "enP8p1s0", "gateway": "192.168.1.1",   "metric": 100,   "family": "ipv4" },
    { "iface": "usb0",     "gateway": "192.168.225.1", "metric": 20100, "family": "ipv4" },
    { "iface": "enP8p1s0", "gateway": "fe80::...",     "metric": 100,   "family": "ipv6" },
    { "iface": "usb0",     "gateway": "fe80::...",     "metric": 20100, "family": "ipv6" }
  ],
  "reachability": [
    { "gateway": "192.168.1.1",   "iface": "enP8p1s0", "target_kind": "gateway",
      "reachable": true, "rtt_ms": 0.4,  "fail_streak": 0, "bound": true },
    { "gateway": "192.168.225.1", "iface": "usb0",     "target_kind": "gateway",
      "reachable": true, "rtt_ms": 2.2,  "fail_streak": 0, "bound": true }
  ],
  "probe_available": true
}
```

**Contract rules:** arrays may be empty; `rtt_ms` **omitted** when
`reachable:false` (single encoding); truncation is signalled by flat sibling
booleans emitted **only when true** — `interfaces_truncated` / `routes_truncated`
(top-level) and `addr_truncated` (per interface); `family` on every route from day
one; top-level `probe_available` is `false` when the ICMP probe is disabled or the
socket can't be created, and `true` otherwise (so an empty `reachability` with
`probe_available:true` means "probed, no gateways" — check `default_routes`);
unknown keys ignored (forward-compatible); one `type` per message shape.

**Consumer trust caveats (security review):** `reachable` is an *unauthenticated*
ICMP result — an on-path attacker can forge it; DAWN/MIRAGE must not gate a
security decision on it. The payload broadcasts hostname, MACs, and IPs; publish it
only to a TLS+auth broker on a trusted segment (STAT is a telemetry publisher by
design — see README MQTT setup).

---

## 6. Reachability probe

- **Target:** each active default-route gateway (Kris's call). Honestly labeled
  `target_kind: "gateway"` — on the cellular iface this is the **modem's own IP
  stack** (~2ms), proving the USB link, not the bearer (§1 row 4 / §2.1). Off by
  `NET_PROBE=false`.
- **Method:** unprivileged ICMP echo, `socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP)`.
  **Verified unprivileged** on this box (`ping_group_range = 0 2147483647`, a
  systemd default — no install step). Send `struct icmphdr{type=ICMP_ECHO,
  code=0, id=0, seq=N}` + payload; kernel fills id/checksum and filters replies to
  the socket (reply is ICMP header + payload, no IP header). Match `seq`.
- **Per-path steering:** `SO_BINDTOIFINDEX` (or `SO_BINDTODEVICE`) — **verified
  unprivileged on 5.15** (CAP_NET_RAW requirement dropped in kernel 5.7).
  Isolates paths (binding `192.168.225.1` to the wired iface correctly times out).
  On `EPERM` (pre-5.7): proceed **unbound** (correct-by-LPM for on-link gateways)
  and publish `"bound": false`. **Source-IP bind is removed — it does not steer**
  (`ip route get 1.1.1.1 from 192.168.225.40` → exits the wired iface). Any future
  **off-link** target *requires* device binding.
- **Bounded, not "non-blocking"** (F-H2a): send all echoes first (one socket per
  gateway, bound), then a **single `poll()` on one shared deadline**. Worst-case
  loop stall = `NET_PROBE_TIMEOUT_MS` (once), not N× per dead gateway.
- **Hysteresis** (F-H2b): publish `reachable` (this sample) **and** `fail_streak`
  (consecutive misses, reset on success). DAWN treats "down" as `fail_streak ≥ 2`,
  so one dropped cell packet doesn't announce an outage.
- **RTT clock:** `CLOCK_MONOTONIC` (never negative on NTP step).
- **Graceful degradation:** ICMP socket `EACCES` → log **once** with the
  `ping_group_range` hint, publish `reachability: []`, continue. Never fake
  success, never fail the whole sample.
- **Sockets:** per-sample open/close (negligible at 5s; robust to gateway-set
  changes).

---

## 7. Configuration & CLI

| Config (stat.conf) | CLI | Default | Meaning |
|---|---|---|---|
| `NET_ENABLE` | `--net-disable` | on | master enable (hardware-free, cheap) |
| `NET_INTERVAL_MS` | `--net-interval` | 5000 | poll cadence; validated ∈ [1000, 60000] |
| `NET_PROBE` | `--net-probe-disable` | on | gateway ICMP reachability probe |
| `NET_PROBE_TIMEOUT_MS` | `--net-probe-timeout` | 500 | shared probe deadline (worst-case loop jitter) |

**Throttle uses `CLOCK_MONOTONIC`** in ms — not `time(NULL)` — so it doesn't
glitch when NTP steps the clock at link-up (F-M3). Env parsing lives in
`network_config_from_env()` inside the module (keeps `oasis-stat.c` thin, F-M5).

**Pre-existing fix (Kris approved):** the BMS throttle at `oasis-stat.c:1196`
uses `time(NULL)` + integer `interval_ms/1000` — same latent glitch. Convert it to
`CLOCK_MONOTONIC` in this change (per CLAUDE.md "fix pre-existing on merit"),
re-run the BMS-adjacent tests.

---

## 8. Testing

- **`tests/test_network_monitor.c`** (Unity, host-only, into ctest), parsers driven
  via `fmemopen()`:
  1. `/proc/net/route` + `/proc/net/ipv6_route` parse — incl. dual-default,
     hex→binary endianness, metric column, **malformed lines** (short line,
     over-long iface, 33-char hex) **under ASan/UBSan** — confirm the bounds checks
     trip *without* the fix (per CLAUDE.md).
  2. `/proc/net/if_inet6` flag filtering: deprecated/temporary/link-local excluded,
     stable GUA/ULA kept.
  3. Interface filter: veth/docker/br/lo/can0 excluded; en/usb/wl included; down
     physical kept.
  4. ICMP reply parse on a canned 18-byte reply (seq/id match).
  5. `build_network_json()` vs a synthetic `network_status_t` → shape asserts.
- **`tests/fixtures/network_v1.json`** — canonical payload; DAWN's
  `test_stat_service.c` parses the **same file** so the wire contract is pinned on
  both sides.
- **On-device:** payload matches `ip addr`/`ip route` exactly (incl.
  `usb0 state:"unknown", up:true, carrier:true`); probe RTTs sane; pulling the
  wired cable flips wired `fail_streak` within 2 polls while `usb0` stays reachable.
- **Under the real systemd unit** (uid `oasis`, `NoNewPrivileges=true`), confirm
  ICMP socket + bind succeed — test the unit, not just the shell.

---

## 9. Explicit non-goals (Phase 1)

- No modem/AT/serial access; no signal/carrier/registration (ECHO).
- No connection management — STAT observes, never controls.
- No **off-link** reachability probe now — gateway-only per decision; an opt-in
  `NET_PROBE_TARGET` (device-bound; verified 1.1.1.1 → 213ms via usb0, 25ms wired)
  is **deferred to P3** as a policy decision.
- No rate math (STAT ships **raw** counters; consumers compute deltas).
- No NM per-device D-Bus connectivity (`IP4/IP6-CONNECTIVITY`) — new dependency,
  deferred (P4, likely never).
- No Wi-Fi SSID/RSSI (nl80211); no DNS-server list (resolved stub makes
  `resolv.conf` uninformative here).
- No DAWN/MIRAGE code (downstream, §2.2).

---

## 10. Downstream / handoff

- **FRIDAY Lead → DAWN (P2):** the four edits in §2.2; consume `Network`, key the
  new `system_status` action on `kind`. Ping *after* STAT's payload is green so the
  §5 contract is frozen. Shared fixture: `tests/fixtures/network_v1.json`.
- **Deferred — ECHO-owned, NOT this work (nor a `dawn/etc/` file):** stable modem
  naming (`usb0`→`cell0`) is entangled with ECHO's live bring-up. `echo/scripts/sim7600-rndis-up.sh`
  hardcodes `IF=usb0` in ~10 places and **generates** the NM `simcom-rndis`
  connection pinned to `ifname usb0` (metric 20100, mtu 1420). A rename therefore
  homes in **`echo/config/20-simcom-rndis.link`** (kin of `echo/config/sim7600-rndis.service`)
  and requires coordinated edits to `sim7600-rndis-up.sh` (`IF=`, the `nmcli … ifname`)
  and ECHO's README. **STAT keys on `driver`/`kind`, so it is immune to the
  interface name and gains nothing from the rename** — it stays a separate ECHO
  housekeeping ticket, tracked in the FRIDAY Lead handoff, not bundled here.
- **ECHO seam (future):** add `bearer_active` (and optionally `wan_ip`) from
  `+CGACT`/`+CGPADDR` so the cellular data bearer has an honest owner. STAT's
  GUA-v6-present is the interim proxy.
- **ECHO / MIRAGE:** no required changes now.

---

## 11. Risks & mitigations

| Risk | Mitigation |
|---|---|
| rndis `operstate=unknown` misread as down | `up = IFF_UP && IFF_RUNNING`; publish `carrier` + raw operstate (§4, F-H1) |
| Probe stalls power loop | Parallel send + single `poll()` deadline; jitter ≤ timeout (§6, F-H2a) |
| One dropped packet flips "down" | `fail_streak` hysteresis; DAWN uses ≥2 (§6, F-H2b) |
| `SO_BINDTODEVICE` privilege / source-IP non-steering | Verified unprivileged; EPERM→unbound (on-link only); source-IP bind removed (§6, F-H3) |
| Modem iface name unstable | STAT keys on `driver`/`kind` (name-immune); `cell0` `.link` rename deferred to ECHO (§4/§10, F-H4) |
| NM penalizes metric +20000 | DAWN selects primary by **min metric** (§4) |
| IPv6 privacy-addr noise in "our IPs" | `/proc/net/if_inet6` flag filter, stable-only (§4, F-M1) |
| NTP step glitches throttle | `CLOCK_MONOTONIC` for net (and BMS) throttle (§7, F-M3) |
| Parser memory-safety surface | Fixed caps + `truncated`; ASan/UBSan malformed-fixture gate (§8) |
| `type` mismatch with DAWN | Frozen §5 contract + shared fixture; one `type` per shape (§2.2) |
| Stepping on ECHO's serial port | Hard rule: STAT never opens `/dev/ttyUSB*` (§2.1) |
| Gateway ping ≠ bearer up | Honest labeling + GUA-v6 bearer proxy + ECHO registration (§1/§2.1, F-H5) |

---

## 12. Phasing (review-approved)

- **P0 — Contract freeze (½ day). [DONE]** `network_monitor.h` +
  `network_monitor_internal.h`, `build_network_json()` declared, canonical
  `tests/fixtures/network_v1.json` written. DAWN owner (FRIDAY Lead) has the fixture.
- **P1a — Enumerate + routes + envelope. [DONE]** `network_monitor.c`: getifaddrs
  IPv4 + `/proc/net/if_inet6` stable IPv6, `/proc/net/route` + `/proc/net/ipv6_route`
  defaults, sysfs fields (up/carrier/mtu/speed/mac/driver/kind) + raw rx/tx counters,
  interface filter (device+driver+ARPHRD), `network_config_from_env()`,
  `CLOCK_MONOTONIC` throttle (BMS throttle converted too), builder + wiring + CLI +
  help + `config/stat.conf`. On-device payload matches `ip addr`/`ip route`
  (usb0 `state:"unknown", up:true`).
- **P1b — Gateway ICMP probe. [DONE]** Per-gateway bound `SOCK_DGRAM` ICMP,
  parallel send + single `poll()` deadline, persistent `fail_streak`, monotonic
  RTT, graceful EACCES→`reachability:[]`. On-device: both gateways reachable,
  `bound:true`. Tests: parsers/filter/classify/reply + envelope, ASan/UBSan clean on
  malformed fixtures. *Still to verify on device: cable-pull streak behavior and a
  run under the real `oasis` systemd unit (not just uid 1000).*
- **P2 — DAWN consumer (downstream, FRIDAY Lead).** §2.2 edits; fixture test.
- **P3 — Optional/stretch.** ICMPv6 link-local nexthop probe; opt-in off-link
  `NET_PROBE_TARGET`; send/harvest split with `SO_TIMESTAMPNS` if jitter matters.
```
