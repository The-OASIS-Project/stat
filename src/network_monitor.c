/**
 * @file network_monitor.c
 * @brief Network telemetry monitoring implementation
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

#include "network_monitor.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "logging.h"
#include "network_monitor_internal.h"
#include "string_utils.h"

/* /proc/net/if_inet6 scope/flag bits we filter on. */
#define IN6_SCOPE_LINK 0x20u
#define IN6_FLAG_TEMPORARY 0x01u
#define IN6_FLAG_DEPRECATED 0x20u

/* RTF flags from /proc route tables. */
#define STAT_RTF_UP 0x0001u
#define STAT_RTF_GATEWAY 0x0002u

#define ICMP_ECHO_PAYLOAD 16

/* Module state. */
static bool s_initialized = false;
static network_config_t s_cfg = { 0 };
static bool s_probe_unavailable_logged = false;

/* Persistent per-(gateway,iface) failure streaks (hysteresis across samples).
 * Stale entries are evicted against the active gateway set before each sample's
 * recording, so departed gateways don't wedge the table. */
typedef struct {
   char gateway[NET_ADDR_LEN];
   char iface[NET_IFNAME_LEN];
   int streak;
   bool used;
} streak_entry_t;
static streak_entry_t s_streaks[NET_MAX_REACH];

/* ----------------------------------------------------------------------- */
/* Small sysfs helpers                                                      */
/* ----------------------------------------------------------------------- */

/** @brief Read a single-line sysfs string, trimming trailing whitespace. */
static bool read_sysfs_str(const char *path, char *buf, size_t size) {
   if (size == 0) {
      return false;
   }
   FILE *fp = fopen(path, "r");
   if (!fp) {
      return false;
   }
   if (!fgets(buf, (int)size, fp)) {
      fclose(fp);
      return false;
   }
   fclose(fp);
   size_t len = strlen(buf);
   while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) {
      buf[--len] = '\0';
   }
   return true;
}

/** @brief Read a signed long long from a sysfs file; return fallback on error. */
static long long read_sysfs_ll(const char *path, long long fallback) {
   char buf[64];
   if (!read_sysfs_str(path, buf, sizeof(buf))) {
      return fallback;
   }
   char *end = NULL;
   errno = 0;
   long long v = strtoll(buf, &end, 10);
   if (errno != 0 || end == buf) {
      return fallback;
   }
   return v;
}

/** @brief Does /sys/class/net/<if>/<sub> exist? */
static bool sysfs_exists(const char *ifname, const char *sub) {
   char path[256];
   snprintf(path, sizeof(path), "/sys/class/net/%s/%s", ifname, sub);
   return access(path, F_OK) == 0;
}

/** @brief Resolve the driver basename via /sys/class/net/<if>/device/driver. */
static void read_driver(const char *ifname, char *out, size_t size) {
   out[0] = '\0';
   char path[256];
   snprintf(path, sizeof(path), "/sys/class/net/%s/device/driver", ifname);
   char target[256];
   ssize_t n = readlink(path, target, sizeof(target) - 1);
   if (n <= 0) {
      return;
   }
   target[n] = '\0';
   const char *base = strrchr(target, '/');
   base = base ? base + 1 : target;
   safe_strncpy(out, base, size);
}

/* ----------------------------------------------------------------------- */
/* Classification (testable)                                                */
/* ----------------------------------------------------------------------- */

network_kind_t network_classify_kind(const char *driver, bool has_wireless) {
   if (driver && driver[0]) {
      if (strcmp(driver, "rndis_host") == 0 || strcmp(driver, "qmi_wwan") == 0 ||
          strcmp(driver, "cdc_mbim") == 0 || strcmp(driver, "cdc_ncm") == 0) {
         return NET_KIND_CELLULAR;
      }
   }
   if (has_wireless) {
      return NET_KIND_WIFI;
   }
   if (driver && driver[0]) {
      return NET_KIND_ETHERNET;
   }
   return NET_KIND_UNKNOWN;
}

static const char *kind_to_str(network_kind_t k) {
   switch (k) {
      case NET_KIND_ETHERNET:
         return "ethernet";
      case NET_KIND_WIFI:
         return "wifi";
      case NET_KIND_CELLULAR:
         return "cellular";
      default:
         return "unknown";
   }
}

/**
 * @brief Interface inclusion filter.
 *
 * Admits interfaces worth reporting while excluding virtual clutter:
 *  - PPP links (mobile-broadband/DSL dial-up) by ARPHRD_PPP — these are virtual
 *    (no device symlink) but are real WAN interfaces.
 *  - device-backed interfaces with a bound driver that are either ARPHRD_ETHER
 *    (ethernet, wifi, RNDIS-tethered modems) OR a known cellular WAN driver
 *    (qmi_wwan/cdc_mbim/cdc_ncm), whose raw-IP mode reports a non-ETHER type.
 *
 * Excludes lo, docker0, br-*, veth*, l4tbr0 (no device symlink), USB-gadget
 * function ports (no bound driver), can0 (ARPHRD type != 1), and tun/wg VPN
 * interfaces (virtual, non-PPP). Down physical NICs are kept.
 */
static bool iface_included(const char *ifname) {
   if (strcmp(ifname, "lo") == 0) {
      return false;
   }

   char path[256];
   snprintf(path, sizeof(path), "/sys/class/net/%s/type", ifname);
   long type = read_sysfs_ll(path, -1);

   /* PPP is virtual (no device symlink) but a real WAN link — admit by type. */
   if (type == ARPHRD_PPP) {
      return true;
   }

   /* Everything else must be device-backed with a bound driver. */
   if (!sysfs_exists(ifname, "device") || !sysfs_exists(ifname, "device/driver")) {
      return false;
   }
   if (type == ARPHRD_ETHER) {
      return true;
   }
   /* Non-ETHER but device-backed: admit only known cellular WAN drivers
    * (raw-IP qmi_wwan/cdc_mbim report ARPHRD_NONE/RAWIP, not ETHER). */
   char driver[NET_DRIVER_LEN];
   read_driver(ifname, driver, sizeof(driver));
   return network_classify_kind(driver, false) == NET_KIND_CELLULAR;
}

/* ----------------------------------------------------------------------- */
/* /proc parsers (testable, FILE*-driven)                                   */
/* ----------------------------------------------------------------------- */

int network_parse_ipv4_routes(FILE *fp, network_route_t *out, size_t cap, size_t *n, bool *trunc) {
   if (!fp || !out || !n) {
      return FAILURE;
   }
   *n = 0;
   if (trunc) {
      *trunc = false;
   }
   char line[512];
   /* Skip header line. */
   if (!fgets(line, sizeof(line), fp)) {
      return SUCCESS; /* empty table */
   }
   while (fgets(line, sizeof(line), fp)) {
      char iface[NET_IFNAME_LEN];
      unsigned long dest = 0, gw = 0, mask = 0;
      unsigned int flags = 0;
      int metric = 0;
      /* Iface Dest Gateway Flags RefCnt Use Metric Mask ... */
      int matched = sscanf(line, "%31s %lx %lx %x %*d %*d %d %lx", iface, &dest, &gw, &flags,
                           &metric, &mask);
      if (matched < 6) {
         continue;
      }
      if (dest != 0 || mask != 0 || !(flags & STAT_RTF_UP)) {
         continue; /* not a default route */
      }
      if (*n >= cap) {
         if (trunc) {
            *trunc = true;
         }
         break;
      }
      network_route_t *r = &out[*n];
      memset(r, 0, sizeof(*r));
      safe_strscpy(r->iface, iface);
      /* Gateway hex is the raw little-endian s_addr; assign directly. */
      struct in_addr addr;
      addr.s_addr = (in_addr_t)gw;
      inet_ntop(AF_INET, &addr, r->gateway, sizeof(r->gateway));
      r->metric = metric;
      r->family = NET_FAMILY_IPV4;
      (*n)++;
   }
   return SUCCESS;
}

int network_parse_ipv6_routes(FILE *fp, network_route_t *out, size_t cap, size_t *n, bool *trunc) {
   if (!fp || !out || !n) {
      return FAILURE;
   }
   *n = 0;
   if (trunc) {
      *trunc = false;
   }
   char line[512];
   while (fgets(line, sizeof(line), fp)) {
      /*
       * /proc/net/ipv6_route columns:
       *  dest(32hex) destplen(2hex) src(32hex) srcplen(2hex)
       *  nexthop(32hex) metric(8hex) refcnt use flags(8hex) iface
       */
      char dest[33], nexthop[33], iface[NET_IFNAME_LEN];
      unsigned int destplen = 0, srcplen = 0;
      unsigned long metric = 0, flags = 0;
      int matched = sscanf(line, "%32s %x %*32s %x %32s %lx %*x %*x %lx %31s", dest, &destplen,
                           &srcplen, nexthop, &metric, &flags, iface);
      if (matched < 7) {
         continue;
      }
      if (destplen != 0 || !(flags & STAT_RTF_GATEWAY)) {
         continue; /* not a default (::/0) gateway route */
      }
      if (strcmp(dest, "00000000000000000000000000000000") != 0) {
         continue;
      }
      if (strlen(nexthop) != 32) {
         continue; /* malformed; avoid reading past the parsed field */
      }
      if (*n >= cap) {
         if (trunc) {
            *trunc = true;
         }
         break;
      }
      network_route_t *r = &out[*n];
      memset(r, 0, sizeof(*r));
      safe_strscpy(r->iface, iface);
      /* nexthop is 32 hex chars -> 16 bytes -> in6_addr. */
      struct in6_addr a6;
      for (int i = 0; i < 16; i++) {
         char byte[3] = { nexthop[i * 2], nexthop[i * 2 + 1], '\0' };
         a6.s6_addr[i] = (uint8_t)strtoul(byte, NULL, 16);
      }
      inet_ntop(AF_INET6, &a6, r->gateway, sizeof(r->gateway));
      r->metric = (long)metric;
      r->family = NET_FAMILY_IPV6;
      (*n)++;
   }
   return SUCCESS;
}

int network_parse_inet6_addrs(FILE *fp,
                              const char *ifname,
                              char out[][NET_ADDR_LEN],
                              size_t cap,
                              size_t *n,
                              bool *trunc) {
   if (!fp || !ifname || !out || !n) {
      return FAILURE;
   }
   *n = 0;
   if (trunc) {
      *trunc = false;
   }
   char line[256];
   while (fgets(line, sizeof(line), fp)) {
      /* addr(32hex) ifindex(2hex) plen(2hex) scope(2hex) flags(2hex) name */
      char addr[33], name[NET_IFNAME_LEN];
      unsigned int scope = 0, flags = 0;
      int matched = sscanf(line, "%32s %*x %*x %x %x %31s", addr, &scope, &flags, name);
      if (matched < 4) {
         continue;
      }
      if (strlen(addr) != 32) {
         continue; /* malformed; avoid reading past the parsed field */
      }
      if (strcmp(name, ifname) != 0) {
         continue;
      }
      if (scope == IN6_SCOPE_LINK) {
         continue; /* link-local */
      }
      if (flags & IN6_FLAG_DEPRECATED) {
         continue;
      }
      if (flags & IN6_FLAG_TEMPORARY) {
         continue; /* stable addresses only */
      }
      if (*n >= cap) {
         if (trunc) {
            *trunc = true;
         }
         break;
      }
      /* Expand 32 hex chars into a colon-grouped address for inet_ntop. */
      struct in6_addr a6;
      for (int i = 0; i < 16; i++) {
         char byte[3] = { addr[i * 2], addr[i * 2 + 1], '\0' };
         a6.s6_addr[i] = (uint8_t)strtoul(byte, NULL, 16);
      }
      inet_ntop(AF_INET6, &a6, out[*n], NET_ADDR_LEN);
      (*n)++;
   }
   return SUCCESS;
}

/* ----------------------------------------------------------------------- */
/* Interface enumeration                                                    */
/* ----------------------------------------------------------------------- */

/** @brief Find an existing accepted iface by name, or NULL. */
static network_iface_t *find_iface(network_status_t *st, const char *name) {
   for (size_t i = 0; i < st->iface_count; i++) {
      if (strcmp(st->ifaces[i].name, name) == 0) {
         return &st->ifaces[i];
      }
   }
   return NULL;
}

/** @brief Find-or-create an accepted iface; NULL if filtered or at capacity. */
static network_iface_t *ensure_iface(network_status_t *st,
                                     const char *name,
                                     unsigned int ifa_flags) {
   network_iface_t *ifc = find_iface(st, name);
   if (ifc) {
      return ifc;
   }
   if (!iface_included(name)) {
      return NULL;
   }
   if (st->iface_count >= NET_MAX_IFACES) {
      st->iface_truncated = true;
      return NULL;
   }
   ifc = &st->ifaces[st->iface_count++];
   memset(ifc, 0, sizeof(*ifc));
   safe_strscpy(ifc->name, name);
   ifc->up = (ifa_flags & IFF_UP) && (ifa_flags & IFF_RUNNING);
   ifc->speed_mbps = -1;
   return ifc;
}

/** @brief Fill sysfs-derived fields for one interface. */
static void fill_iface_sysfs(network_iface_t *ifc) {
   char path[256];

   snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", ifc->name);
   if (!read_sysfs_str(path, ifc->operstate, sizeof(ifc->operstate))) {
      safe_strscpy(ifc->operstate, "unknown");
   }

   snprintf(path, sizeof(path), "/sys/class/net/%s/carrier", ifc->name);
   ifc->carrier = (read_sysfs_ll(path, 0) == 1);

   snprintf(path, sizeof(path), "/sys/class/net/%s/mtu", ifc->name);
   ifc->mtu = (int)read_sysfs_ll(path, 0);

   snprintf(path, sizeof(path), "/sys/class/net/%s/speed", ifc->name);
   ifc->speed_mbps = (int)read_sysfs_ll(path, -1);

   snprintf(path, sizeof(path), "/sys/class/net/%s/address", ifc->name);
   if (!read_sysfs_str(path, ifc->mac, sizeof(ifc->mac))) {
      ifc->mac[0] = '\0';
   }

   snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_bytes", ifc->name);
   ifc->rx_bytes = (unsigned long long)read_sysfs_ll(path, 0);
   snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_bytes", ifc->name);
   ifc->tx_bytes = (unsigned long long)read_sysfs_ll(path, 0);

   read_driver(ifc->name, ifc->driver, sizeof(ifc->driver));
   ifc->kind = network_classify_kind(ifc->driver, sysfs_exists(ifc->name, "wireless"));
}

/** @brief Enumerate interfaces + IPv4 addresses via getifaddrs. */
static int enumerate_interfaces(network_status_t *st) {
   struct ifaddrs *ifap = NULL;
   if (getifaddrs(&ifap) != 0) {
      OLOG_ERROR("network: getifaddrs failed: %s", strerror(errno));
      return FAILURE;
   }

   /* getifaddrs returns consecutive entries per interface; remember the last
    * rejected name so a rejected virtual iface isn't re-filtered (3 syscalls)
    * for each of its address entries. */
   char last_rejected[NET_IFNAME_LEN] = "";
   for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
      if (!ifa->ifa_name) {
         continue;
      }
      if (last_rejected[0] && strcmp(ifa->ifa_name, last_rejected) == 0) {
         continue;
      }
      network_iface_t *ifc = ensure_iface(st, ifa->ifa_name, ifa->ifa_flags);
      if (!ifc) {
         safe_strscpy(last_rejected, ifa->ifa_name);
         continue;
      }
      if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
         char ip[NET_ADDR_LEN];
         struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
         if (inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip))) {
            if (ifc->ipv4_count < NET_MAX_ADDRS_PER_IFACE) {
               safe_strscpy(ifc->ipv4[ifc->ipv4_count], ip);
               ifc->ipv4_count++;
            } else {
               ifc->ipv4_truncated = true;
            }
         }
      }
   }
   freeifaddrs(ifap);

   /* Per-iface sysfs fields + stable IPv6 addresses. */
   for (size_t i = 0; i < st->iface_count; i++) {
      network_iface_t *ifc = &st->ifaces[i];
      fill_iface_sysfs(ifc);

      FILE *fp = fopen("/proc/net/if_inet6", "r");
      if (fp) {
         network_parse_inet6_addrs(fp, ifc->name, ifc->ipv6, NET_MAX_ADDRS_PER_IFACE,
                                   &ifc->ipv6_count, &ifc->ipv6_truncated);
         fclose(fp);
      }
   }
   return SUCCESS;
}

/** @brief Resolve IPv4 + IPv6 default routes. */
static void enumerate_routes(network_status_t *st) {
   size_t n4 = 0;
   bool t4 = false;
   FILE *fp = fopen("/proc/net/route", "r");
   if (fp) {
      network_parse_ipv4_routes(fp, st->routes, NET_MAX_ROUTES, &n4, &t4);
      fclose(fp);
   }
   st->route_count = n4;
   st->route_truncated = t4;

   size_t n6 = 0;
   bool t6 = false;
   fp = fopen("/proc/net/ipv6_route", "r");
   if (fp && st->route_count < NET_MAX_ROUTES) {
      network_parse_ipv6_routes(fp, &st->routes[st->route_count], NET_MAX_ROUTES - st->route_count,
                                &n6, &t6);
      st->route_count += n6;
   }
   if (fp) {
      fclose(fp);
   }
   if (t6) {
      st->route_truncated = true;
   }
}

/* ----------------------------------------------------------------------- */
/* ICMP reachability probe                                                  */
/* ----------------------------------------------------------------------- */

bool network_icmp_reply_ok(const uint8_t *buf, size_t len, uint16_t expect_seq) {
   if (!buf || len < sizeof(struct icmphdr)) {
      return false;
   }
   const struct icmphdr *hdr = (const struct icmphdr *)buf;
   if (hdr->type != ICMP_ECHOREPLY) {
      return false;
   }
   return ntohs(hdr->un.echo.sequence) == expect_seq;
}

/** @brief Evict every streak entry (probe disabled/unavailable/no gateways). */
static void streak_evict_all(void) {
   for (int i = 0; i < NET_MAX_REACH; i++) {
      s_streaks[i].used = false;
   }
}

/**
 * @brief Look up / create the persistent streak slot for a (gateway, iface).
 *
 * Keyed on both gateway and iface so the same gateway IP on two interfaces
 * tracks independently. The caller MUST have evicted stale (departed-gateway)
 * entries against the active set first (see streak_retain in probe_reachability);
 * with that done, active gateways (<= NET_MAX_REACH) always find a free slot, so
 * a NULL return means only a genuine programming-invariant break.
 */
static int *streak_for(const char *gateway, const char *iface) {
   for (int i = 0; i < NET_MAX_REACH; i++) {
      if (s_streaks[i].used && strcmp(s_streaks[i].gateway, gateway) == 0 &&
          strcmp(s_streaks[i].iface, iface) == 0) {
         return &s_streaks[i].streak;
      }
   }
   for (int i = 0; i < NET_MAX_REACH; i++) {
      if (!s_streaks[i].used) {
         s_streaks[i].used = true;
         safe_strscpy(s_streaks[i].gateway, gateway);
         safe_strscpy(s_streaks[i].iface, iface);
         s_streaks[i].streak = 0;
         return &s_streaks[i].streak;
      }
   }
   return NULL;
}

static double ts_diff_ms(const struct timespec *a, const struct timespec *b) {
   return (double)(b->tv_sec - a->tv_sec) * 1000.0 + (double)(b->tv_nsec - a->tv_nsec) / 1e6;
}

/**
 * @brief Probe each IPv4 default-route gateway with one bounded ICMP round.
 *
 * Sends all echoes first, then waits on a single shared poll() deadline so the
 * worst-case stall is one timeout, not one per unreachable gateway.
 */
static void probe_reachability(network_status_t *st) {
   st->probe_available = true; /* optimistic; cleared only if no socket opens */

   /* Collect distinct IPv4 gateways from the default routes. */
   struct {
      const network_route_t *route;
      int fd;
      uint16_t seq;
      struct timespec sent;
      bool replied;
      bool done; /* retired from the poll set (replied or errored) */
      bool bound;
   } slots[NET_MAX_REACH];
   size_t nslots = 0;

   for (size_t i = 0; i < st->route_count && nslots < NET_MAX_REACH; i++) {
      const network_route_t *r = &st->routes[i];
      if (r->family != NET_FAMILY_IPV4) {
         continue; /* ICMPv6 probing deferred (design §12 P3) */
      }
      bool dup = false;
      for (size_t j = 0; j < nslots; j++) {
         if (strcmp(slots[j].route->gateway, r->gateway) == 0) {
            dup = true;
            break;
         }
      }
      if (dup) {
         continue;
      }
      slots[nslots].route = r;
      slots[nslots].fd = -1;
      slots[nslots].replied = false;
      slots[nslots].done = false;
      slots[nslots].bound = false;
      nslots++;
   }

   if (nslots == 0) {
      streak_evict_all(); /* no gateways -> no streaks to keep */
      return;
   }

   uint16_t base_seq = (uint16_t)(getpid() & 0xffff);
   bool any_socket = false;

   for (size_t i = 0; i < nslots; i++) {
      int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
      if (fd < 0) {
         if ((errno == EACCES || errno == EPERM) && !s_probe_unavailable_logged) {
            OLOG_WARNING("network: ICMP probe unavailable (%s); check "
                         "net.ipv4.ping_group_range. Reporting reachability as empty.",
                         strerror(errno));
            s_probe_unavailable_logged = true;
         }
         continue;
      }
      any_socket = true;

      /* Steer the probe out the route's interface (verified unprivileged on
       * 5.15). On EPERM, proceed unbound — correct for on-link gateways. */
      if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, slots[i].route->iface,
                     (socklen_t)strlen(slots[i].route->iface)) == 0) {
         slots[i].bound = true;
      }

      struct sockaddr_in dst;
      memset(&dst, 0, sizeof(dst));
      dst.sin_family = AF_INET;
      if (inet_pton(AF_INET, slots[i].route->gateway, &dst.sin_addr) != 1) {
         close(fd);
         continue;
      }

      uint8_t pkt[sizeof(struct icmphdr) + ICMP_ECHO_PAYLOAD];
      memset(pkt, 0, sizeof(pkt));
      struct icmphdr *hdr = (struct icmphdr *)pkt;
      hdr->type = ICMP_ECHO;
      hdr->code = 0;
      hdr->un.echo.id = 0; /* kernel rewrites id for SOCK_DGRAM ICMP */
      slots[i].seq = (uint16_t)(base_seq + i);
      hdr->un.echo.sequence = htons(slots[i].seq);

      clock_gettime(CLOCK_MONOTONIC, &slots[i].sent);
      ssize_t sent = sendto(fd, pkt, sizeof(pkt), 0, (struct sockaddr *)&dst, sizeof(dst));
      if (sent < 0) {
         close(fd);
         continue;
      }
      slots[i].fd = fd;
   }

   if (!any_socket) {
      st->probe_available = false;
      streak_evict_all(); /* probe unavailable -> streaks are meaningless */
      return;
   }

   /* Evict streaks for departed gateways BEFORE recording, using the known
    * active set. Running the sweep here (not after) guarantees streak_for()
    * finds a free slot for every active gateway — active count <= nslots <=
    * NET_MAX_REACH = table size — with no risk of stealing an active gateway's
    * not-yet-recorded slot. */
   for (int e = 0; e < NET_MAX_REACH; e++) {
      if (!s_streaks[e].used) {
         continue;
      }
      bool active = false;
      for (size_t i = 0; i < nslots; i++) {
         if (strcmp(s_streaks[e].gateway, slots[i].route->gateway) == 0 &&
             strcmp(s_streaks[e].iface, slots[i].route->iface) == 0) {
            active = true;
            break;
         }
      }
      if (!active) {
         s_streaks[e].used = false;
      }
   }

   /* Single shared deadline across all sockets. */
   struct timespec deadline;
   clock_gettime(CLOCK_MONOTONIC, &deadline);
   long timeout_ms = s_cfg.probe_timeout_ms;

   for (;;) {
      struct pollfd pfds[NET_MAX_REACH];
      size_t map[NET_MAX_REACH];
      nfds_t nf = 0;
      for (size_t i = 0; i < nslots; i++) {
         if (slots[i].fd >= 0 && !slots[i].done) {
            pfds[nf].fd = slots[i].fd;
            pfds[nf].events = POLLIN;
            pfds[nf].revents = 0;
            map[nf] = i;
            nf++;
         }
      }
      if (nf == 0) {
         break;
      }

      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      long elapsed = (long)ts_diff_ms(&deadline, &now);
      long remaining = timeout_ms - elapsed;
      if (remaining <= 0) {
         break;
      }

      int pr = poll(pfds, nf, (int)remaining);
      if (pr <= 0) {
         break; /* timeout or error */
      }
      for (nfds_t k = 0; k < nf; k++) {
         size_t i = map[k];
         /* Retire an errored socket (e.g. gateway replied with an ICMP error):
          * drain the pending error so poll() stops re-reporting it, and exclude
          * it from the next round. It stays !replied so it records unreachable
          * below. Without this, poll() would spin on POLLERR until the deadline. */
         if (pfds[k].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            uint8_t drain[128];
            (void)recv(slots[i].fd, drain, sizeof(drain), 0);
            slots[i].done = true;
            continue;
         }
         if (!(pfds[k].revents & POLLIN)) {
            continue;
         }
         uint8_t rbuf[128];
         ssize_t rn = recv(slots[i].fd, rbuf, sizeof(rbuf), 0);
         if (rn > 0 && network_icmp_reply_ok(rbuf, (size_t)rn, slots[i].seq)) {
            if (st->reach_count >= NET_MAX_REACH) {
               break;
            }
            struct timespec rt;
            clock_gettime(CLOCK_MONOTONIC, &rt);
            slots[i].replied = true;
            slots[i].done = true;

            network_reach_t *out = &st->reach[st->reach_count++];
            memset(out, 0, sizeof(*out));
            safe_strscpy(out->gateway, slots[i].route->gateway);
            safe_strscpy(out->iface, slots[i].route->iface);
            out->reachable = true;
            out->rtt_ms = ts_diff_ms(&slots[i].sent, &rt);
            out->bound = slots[i].bound;
            int *streak = streak_for(out->gateway, out->iface);
            if (streak) {
               *streak = 0;
            }
            out->fail_streak = 0;
         }
      }
   }

   /* Anything not replied is unreachable this sample. */
   for (size_t i = 0; i < nslots; i++) {
      if (slots[i].fd >= 0) {
         close(slots[i].fd);
      }
      if (slots[i].replied || st->reach_count >= NET_MAX_REACH) {
         continue;
      }
      network_reach_t *out = &st->reach[st->reach_count++];
      memset(out, 0, sizeof(*out));
      safe_strscpy(out->gateway, slots[i].route->gateway);
      safe_strscpy(out->iface, slots[i].route->iface);
      out->reachable = false;
      out->bound = slots[i].bound;
      int *streak = streak_for(out->gateway, out->iface);
      if (streak) {
         (*streak)++;
         out->fail_streak = *streak;
      } else {
         out->fail_streak = 1;
      }
   }
}

/* ----------------------------------------------------------------------- */
/* Public API                                                               */
/* ----------------------------------------------------------------------- */

int network_monitor_init(const network_config_t *cfg) {
   if (cfg) {
      s_cfg = *cfg;
   } else {
      s_cfg.probe_enabled = true;
      s_cfg.probe_timeout_ms = 500;
      s_cfg.interval_ms = 5000;
   }
   /* Clamp defensively for direct API callers (env/CLI paths already clamp);
    * caps worst-case per-sample probe stall. */
   if (s_cfg.probe_timeout_ms <= 0) {
      s_cfg.probe_timeout_ms = 500;
   } else if (s_cfg.probe_timeout_ms > 5000) {
      s_cfg.probe_timeout_ms = 5000;
   }
   memset(s_streaks, 0, sizeof(s_streaks));
   s_probe_unavailable_logged = false;
   s_initialized = true;
   OLOG_INFO("Network monitoring initialized (probe %s, timeout %d ms)",
             s_cfg.probe_enabled ? "on" : "off", s_cfg.probe_timeout_ms);
   return SUCCESS;
}

int network_monitor_sample(network_status_t *out) {
   if (!out) {
      return FAILURE;
   }
   if (!s_initialized) {
      if (network_monitor_init(NULL) != SUCCESS) {
         return FAILURE;
      }
   }

   network_status_t st;
   memset(&st, 0, sizeof(st));
   if (gethostname(st.hostname, sizeof(st.hostname)) != 0) {
      st.hostname[0] = '\0';
   }
   st.hostname[sizeof(st.hostname) - 1] = '\0';

   if (enumerate_interfaces(&st) != SUCCESS) {
      return FAILURE; /* leave caller's buffer untouched */
   }
   enumerate_routes(&st);

   if (s_cfg.probe_enabled) {
      probe_reachability(&st);
   } else {
      st.probe_available = false;
   }

   *out = st;
   return SUCCESS;
}

void network_monitor_cleanup(void) {
   s_initialized = false;
   OLOG_INFO("Network monitoring cleaned up");
}

const char *network_kind_str(network_kind_t kind) {
   return kind_to_str(kind);
}

int network_config_from_env(network_config_t *out) {
   if (!out) {
      return FAILURE;
   }
   out->probe_enabled = true;
   out->probe_timeout_ms = 500;
   out->interval_ms = 5000;

   const char *en = getenv("NET_ENABLE");
   bool enabled = true;
   if (en && (strcasecmp(en, "false") == 0 || strcmp(en, "0") == 0 || strcasecmp(en, "off") == 0 ||
              strcasecmp(en, "no") == 0)) {
      enabled = false;
   }

   const char *iv = getenv("NET_INTERVAL_MS");
   if (iv && iv[0]) {
      long v = strtol(iv, NULL, 10);
      if (v < 1000) {
         v = 1000;
      }
      if (v > 60000) {
         v = 60000;
      }
      out->interval_ms = (int)v;
   }

   const char *pr = getenv("NET_PROBE");
   if (pr && (strcasecmp(pr, "false") == 0 || strcmp(pr, "0") == 0 || strcasecmp(pr, "off") == 0 ||
              strcasecmp(pr, "no") == 0)) {
      out->probe_enabled = false;
   }

   const char *pt = getenv("NET_PROBE_TIMEOUT_MS");
   if (pt && pt[0]) {
      long v = strtol(pt, NULL, 10);
      if (v < 50) {
         v = 50;
      }
      if (v > 5000) {
         v = 5000;
      }
      out->probe_timeout_ms = (int)v;
   }

   return enabled ? SUCCESS : FAILURE;
}
