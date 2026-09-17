/**
 * @file network_monitor.h
 * @brief Network telemetry monitoring (interfaces, routes, reachability)
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

#ifndef NETWORK_MONITOR_H
#define NETWORK_MONITOR_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * House return-code standard for new OASIS modules (see CLAUDE.md):
 * SUCCESS (0) / FAILURE (1). Defined defensively in case a future shared
 * header provides them.
 */
#ifndef SUCCESS
#define SUCCESS 0
#endif
#ifndef FAILURE
#define FAILURE 1
#endif

/* Fixed capacities — no per-sample heap growth (see design §3). */
#define NET_MAX_IFACES 16
#define NET_MAX_ADDRS_PER_IFACE 8
#define NET_MAX_ROUTES 8
#define NET_MAX_REACH 8

#define NET_IFNAME_LEN 32 /* IFNAMSIZ is 16; padded */
#define NET_ADDR_LEN 64   /* INET6_ADDRSTRLEN is 46 */
#define NET_MAC_LEN 20    /* "xx:xx:xx:xx:xx:xx" + slack */
#define NET_DRIVER_LEN 32
#define NET_OPERSTATE_LEN 16
#define NET_HOSTNAME_LEN 65 /* HOST_NAME_MAX (64) + NUL */

/** @brief Coarse interface classification, derived from driver (never name). */
typedef enum {
   NET_KIND_UNKNOWN = 0,
   NET_KIND_ETHERNET,
   NET_KIND_WIFI,
   NET_KIND_CELLULAR
} network_kind_t;

/** @brief IP address family for routes. */
typedef enum {
   NET_FAMILY_IPV4 = 4,
   NET_FAMILY_IPV6 = 6
} network_family_t;

/** @brief A single monitored network interface. */
typedef struct {
   char name[NET_IFNAME_LEN];
   char driver[NET_DRIVER_LEN];
   network_kind_t kind;
   char operstate[NET_OPERSTATE_LEN]; /* raw /sys operstate string */
   bool up;                           /* IFF_UP && IFF_RUNNING */
   bool carrier;                      /* /sys/class/net/<if>/carrier */
   int mtu;
   int speed_mbps; /* -1 if not applicable/unknown */
   char mac[NET_MAC_LEN];

   char ipv4[NET_MAX_ADDRS_PER_IFACE][NET_ADDR_LEN];
   size_t ipv4_count;
   bool ipv4_truncated;

   char ipv6[NET_MAX_ADDRS_PER_IFACE][NET_ADDR_LEN];
   size_t ipv6_count;
   bool ipv6_truncated;

   unsigned long long rx_bytes; /* raw cumulative counters, no rate math */
   unsigned long long tx_bytes;
} network_iface_t;

/** @brief A default route (one per address family per interface). */
typedef struct {
   char iface[NET_IFNAME_LEN];
   char gateway[NET_ADDR_LEN];
   long metric;
   network_family_t family;
} network_route_t;

/** @brief First-hop reachability result for one default-route gateway. */
typedef struct {
   char gateway[NET_ADDR_LEN];
   char iface[NET_IFNAME_LEN];
   bool reachable;  /* this sample */
   double rtt_ms;   /* valid only when reachable */
   int fail_streak; /* consecutive misses; reset on success (hysteresis) */
   bool bound;      /* SO_BINDTOIFINDEX/SO_BINDTODEVICE succeeded */
} network_reach_t;

/** @brief Full network snapshot, caller-owned. */
typedef struct {
   char hostname[NET_HOSTNAME_LEN];

   network_iface_t ifaces[NET_MAX_IFACES];
   size_t iface_count;
   bool iface_truncated;

   network_route_t routes[NET_MAX_ROUTES];
   size_t route_count;
   bool route_truncated;

   network_reach_t reach[NET_MAX_REACH];
   size_t reach_count;

   bool probe_available; /* false => ICMP socket unavailable; reach[] empty */
} network_status_t;

/** @brief Runtime configuration for the network monitor. */
typedef struct {
   bool probe_enabled;   /* run the gateway ICMP reachability probe */
   int probe_timeout_ms; /* shared probe deadline (worst-case loop jitter) */
   int interval_ms;      /* publish cadence (throttle handled by caller) */
} network_config_t;

/**
 * @brief Initialize network monitoring with the given configuration.
 *
 * @param cfg Configuration (copied). May be NULL to accept built-in defaults.
 * @return SUCCESS on success, FAILURE on error.
 */
int network_monitor_init(const network_config_t *cfg);

/**
 * @brief Take one network sample into a caller-owned status struct.
 *
 * Enumerates interfaces, resolves IPv4/IPv6 default routes, and (if enabled)
 * probes gateway reachability. Never blocks longer than the configured probe
 * timeout. On a hard failure the previous @p out contents are left untouched.
 *
 * @param out Caller-owned status buffer to populate (must be non-NULL).
 * @return SUCCESS on success, FAILURE on error.
 */
int network_monitor_sample(network_status_t *out);

/**
 * @brief Release network monitoring resources.
 */
void network_monitor_cleanup(void);

/**
 * @brief Return the lowercase string name for an interface kind.
 *
 * @param kind Interface classification.
 * @return "ethernet" / "wifi" / "cellular" / "unknown" (never NULL).
 */
const char *network_kind_str(network_kind_t kind);

/**
 * @brief Populate a configuration from NET_* environment variables.
 *
 * Applies defaults, then overrides from NET_ENABLE / NET_INTERVAL_MS /
 * NET_PROBE / NET_PROBE_TIMEOUT_MS. Interval is clamped to [1000, 60000] ms.
 *
 * @param out Configuration to fill (must be non-NULL).
 * @return SUCCESS if enabled, FAILURE if NET_ENABLE disables monitoring.
 */
int network_config_from_env(network_config_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_MONITOR_H */
