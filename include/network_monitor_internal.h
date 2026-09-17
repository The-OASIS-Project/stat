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
 * Network monitor internal helpers exposed for unit testing. Not part of the
 * public API — only network_monitor.c and test files should include this.
 * The parsers take a FILE* so tests can drive them with fmemopen() over
 * /proc-format fixtures.
 */

#ifndef NETWORK_MONITOR_INTERNAL_H
#define NETWORK_MONITOR_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "network_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse default IPv4 routes from a /proc/net/route stream.
 *
 * Selects rows with Destination==0, Mask==0, and RTF_UP. Gateway hex is the
 * kernel's raw little-endian s_addr printed with %08X; it is converted without
 * byte-swapping (correct on all host endianness).
 *
 * @param fp     Open stream in /proc/net/route format (header line skipped).
 * @param out    Output array of routes (family set to NET_FAMILY_IPV4).
 * @param cap    Capacity of @p out.
 * @param n      Set to the number of routes written.
 * @param trunc  Set true if more defaults existed than @p cap.
 * @return SUCCESS or FAILURE.
 */
int network_parse_ipv4_routes(FILE *fp, network_route_t *out, size_t cap, size_t *n, bool *trunc);

/**
 * @brief Parse default IPv6 routes from a /proc/net/ipv6_route stream.
 *
 * Selects rows whose destination is ::/0 (all-zero dest, prefix length 0) with
 * the RTF_GATEWAY (0x2) flag set.
 *
 * @param fp     Open stream in /proc/net/ipv6_route format.
 * @param out    Output array of routes (family set to NET_FAMILY_IPV6).
 * @param cap    Capacity of @p out.
 * @param n      Set to the number of routes written.
 * @param trunc  Set true if more defaults existed than @p cap.
 * @return SUCCESS or FAILURE.
 */
int network_parse_ipv6_routes(FILE *fp, network_route_t *out, size_t cap, size_t *n, bool *trunc);

/**
 * @brief Parse stable IPv6 addresses for one interface from /proc/net/if_inet6.
 *
 * Columns: addr(32 hex) ifindex plen scope flags name. Excludes link-local
 * (scope 0x20), deprecated (flags 0x20), and — by default — temporary/privacy
 * (flags 0x01) addresses, so only stable GUA/ULA are returned.
 *
 * @param fp      Open stream in /proc/net/if_inet6 format.
 * @param ifname  Interface name to match.
 * @param out     Output array of NET_ADDR_LEN strings.
 * @param cap     Capacity of @p out.
 * @param n       Set to the number of addresses written.
 * @param trunc   Set true if more addresses existed than @p cap.
 * @return SUCCESS or FAILURE.
 */
int network_parse_inet6_addrs(FILE *fp,
                              const char *ifname,
                              char out[][NET_ADDR_LEN],
                              size_t cap,
                              size_t *n,
                              bool *trunc);

/**
 * @brief Classify an interface from its driver name and wireless presence.
 *
 * rndis_host / qmi_wwan / cdc_mbim / cdc_ncm -> cellular; wireless present ->
 * wifi; otherwise ethernet (unknown driver with no other signal -> unknown).
 *
 * @param driver       Driver basename (may be empty).
 * @param has_wireless Whether /sys/class/net/<if>/wireless exists.
 * @return network_kind_t classification.
 */
network_kind_t network_classify_kind(const char *driver, bool has_wireless);

/**
 * @brief Validate an unprivileged-ICMP echo reply payload.
 *
 * The SOCK_DGRAM/IPPROTO_ICMP reply is an ICMP header (type 0) plus payload,
 * with no IP header. Confirms echo-reply type and matching sequence.
 *
 * @param buf         Reply buffer (as received).
 * @param len         Reply length in bytes.
 * @param expect_seq  Sequence number sent.
 * @return true if a valid matching echo reply.
 */
bool network_icmp_reply_ok(const uint8_t *buf, size_t len, uint16_t expect_seq);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_MONITOR_INTERNAL_H */
