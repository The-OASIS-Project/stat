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
 * Unit tests for network_monitor.c parsers/classifiers and the Network JSON
 * envelope. No broker and no live network are required — /proc-format inputs
 * are fed through fmemopen().
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <json-c/json.h>
#include <netinet/ip_icmp.h>
#include <stdio.h>
#include <string.h>

#include "mqtt_publisher_internal.h"
#include "network_monitor.h"
#include "network_monitor_internal.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

/* ---- Helpers ---- */

static FILE *mem(const char *s) {
   return fmemopen((void *)s, strlen(s), "r");
}

/* ---- /proc/net/route (IPv4 defaults) ---- */

void test_ipv4_routes_dual_default(void) {
   const char *proc =
       "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n"
       "enP8p1s0\t00000000\t0101A8C0\t0003\t0\t0\t100\t00000000\t0\t0\t0\n"
       "usb0\t00000000\t01E1A8C0\t0003\t0\t0\t20100\t00000000\t0\t0\t0\n"
       "br-x\t0000FEA9\t00000000\t0001\t0\t0\t1000\t0000FFFF\t0\t0\t0\n";
   FILE *fp = mem(proc);
   network_route_t routes[NET_MAX_ROUTES];
   size_t n = 0;
   bool trunc = false;
   TEST_ASSERT_EQUAL_INT(SUCCESS,
                         network_parse_ipv4_routes(fp, routes, NET_MAX_ROUTES, &n, &trunc));
   fclose(fp);

   TEST_ASSERT_EQUAL_UINT(2, n);
   TEST_ASSERT_FALSE(trunc);
   /* Little-endian hex 0101A8C0 -> 192.168.1.1 (no byte-swap). */
   TEST_ASSERT_EQUAL_STRING("enP8p1s0", routes[0].iface);
   TEST_ASSERT_EQUAL_STRING("192.168.1.1", routes[0].gateway);
   TEST_ASSERT_EQUAL_INT(100, (int)routes[0].metric);
   TEST_ASSERT_EQUAL_INT(NET_FAMILY_IPV4, routes[0].family);
   TEST_ASSERT_EQUAL_STRING("192.168.225.1", routes[1].gateway);
   TEST_ASSERT_EQUAL_INT(20100, (int)routes[1].metric);
}

void test_ipv4_routes_malformed_lines_skipped(void) {
   const char *proc = "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\n"
                      "garbage\n"
                      "\n"
                      "shorty\t00\n"
                      "enP8p1s0\t00000000\t0101A8C0\t0003\t0\t0\t100\t00000000\n";
   FILE *fp = mem(proc);
   network_route_t routes[NET_MAX_ROUTES];
   size_t n = 0;
   bool trunc = false;
   TEST_ASSERT_EQUAL_INT(SUCCESS,
                         network_parse_ipv4_routes(fp, routes, NET_MAX_ROUTES, &n, &trunc));
   fclose(fp);
   TEST_ASSERT_EQUAL_UINT(1, n);
   TEST_ASSERT_EQUAL_STRING("192.168.1.1", routes[0].gateway);
}

void test_ipv4_routes_truncation(void) {
   const char *proc = "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\n"
                      "a\t00000000\t0101A8C0\t0003\t0\t0\t1\t00000000\n"
                      "b\t00000000\t0101A8C0\t0003\t0\t0\t2\t00000000\n";
   FILE *fp = mem(proc);
   network_route_t routes[1];
   size_t n = 0;
   bool trunc = false;
   network_parse_ipv4_routes(fp, routes, 1, &n, &trunc);
   fclose(fp);
   TEST_ASSERT_EQUAL_UINT(1, n);
   TEST_ASSERT_TRUE(trunc);
}

/* ---- /proc/net/ipv6_route (IPv6 defaults) ---- */

void test_ipv6_routes_default_only(void) {
   const char *proc =
       /* default ::/0 via link-local, flags 0x3 (UP|GATEWAY) -> keep */
       "00000000000000000000000000000000 00 "
       "00000000000000000000000000000000 00 "
       "fe80000000000000c206c3fffeaa8f88 00000064 00000000 00000000 00000003 enP8p1s0\n"
       /* a /64 connected route, flags 0x1 (no GATEWAY) -> skip */
       "2607fb907c1cccc70000000000000000 40 "
       "00000000000000000000000000000000 00 "
       "00000000000000000000000000000000 00000100 00000000 00000000 00000001 usb0\n";
   FILE *fp = mem(proc);
   network_route_t routes[NET_MAX_ROUTES];
   size_t n = 0;
   bool trunc = false;
   TEST_ASSERT_EQUAL_INT(SUCCESS,
                         network_parse_ipv6_routes(fp, routes, NET_MAX_ROUTES, &n, &trunc));
   fclose(fp);

   TEST_ASSERT_EQUAL_UINT(1, n);
   TEST_ASSERT_EQUAL_STRING("enP8p1s0", routes[0].iface);
   TEST_ASSERT_EQUAL_STRING("fe80::c206:c3ff:feaa:8f88", routes[0].gateway);
   TEST_ASSERT_EQUAL_INT(100, (int)routes[0].metric);
   TEST_ASSERT_EQUAL_INT(NET_FAMILY_IPV6, routes[0].family);
}

/* ---- /proc/net/if_inet6 (stable address filtering) ---- */

void test_inet6_addrs_stable_only(void) {
   const char *proc =
       /* global stable: scope 00, flags 00 -> keep (32 hex chars) */
       "2600170251ae06ef148233639c075ef5 02 40 00 00 enP8p1s0\n"
       /* link-local: scope 20 -> skip */
       "fe80000000000000224a85b29fca17ad 02 40 20 80 enP8p1s0\n"
       /* temporary+deprecated: flags 21 -> skip */
       "2600170251ae06ef00000000000dabc0 02 40 00 21 enP8p1s0\n"
       /* malformed short address (< 32 hex) -> skip, no over-read */
       "2600170251ae 02 40 00 00 enP8p1s0\n"
       /* different interface -> skip */
       "2607fb907c1cccc766279487834baf2b 05 40 00 00 usb0\n";
   FILE *fp = mem(proc);
   char addrs[NET_MAX_ADDRS_PER_IFACE][NET_ADDR_LEN];
   size_t n = 0;
   bool trunc = false;
   TEST_ASSERT_EQUAL_INT(SUCCESS, network_parse_inet6_addrs(fp, "enP8p1s0", addrs,
                                                            NET_MAX_ADDRS_PER_IFACE, &n, &trunc));
   fclose(fp);

   TEST_ASSERT_EQUAL_UINT(1, n);
   TEST_ASSERT_EQUAL_STRING("2600:1702:51ae:6ef:1482:3363:9c07:5ef5", addrs[0]);
}

/* ---- kind classification ---- */

void test_classify_kind(void) {
   TEST_ASSERT_EQUAL_INT(NET_KIND_CELLULAR, network_classify_kind("rndis_host", false));
   TEST_ASSERT_EQUAL_INT(NET_KIND_CELLULAR, network_classify_kind("qmi_wwan", false));
   TEST_ASSERT_EQUAL_INT(NET_KIND_CELLULAR, network_classify_kind("cdc_mbim", false));
   TEST_ASSERT_EQUAL_INT(NET_KIND_CELLULAR, network_classify_kind("cdc_ncm", false));
   TEST_ASSERT_EQUAL_INT(NET_KIND_WIFI, network_classify_kind("rtl88x2ce", true));
   TEST_ASSERT_EQUAL_INT(NET_KIND_ETHERNET, network_classify_kind("r8168", false));
   TEST_ASSERT_EQUAL_INT(NET_KIND_UNKNOWN, network_classify_kind("", false));
   /* Cellular driver wins even if a wireless dir somehow exists. */
   TEST_ASSERT_EQUAL_INT(NET_KIND_CELLULAR, network_classify_kind("rndis_host", true));
}

/* ---- ICMP reply validation ---- */

void test_icmp_reply_ok(void) {
   uint8_t buf[8];
   struct icmphdr *h = (struct icmphdr *)buf;
   memset(buf, 0, sizeof(buf));
   h->type = ICMP_ECHOREPLY;
   h->un.echo.sequence = htons(42);

   TEST_ASSERT_TRUE(network_icmp_reply_ok(buf, sizeof(buf), 42));
   TEST_ASSERT_FALSE(network_icmp_reply_ok(buf, sizeof(buf), 43)); /* wrong seq */
   h->type = ICMP_ECHO;
   TEST_ASSERT_FALSE(network_icmp_reply_ok(buf, sizeof(buf), 42)); /* not a reply */
   TEST_ASSERT_FALSE(network_icmp_reply_ok(buf, 4, 42));           /* too short */
   TEST_ASSERT_FALSE(network_icmp_reply_ok(NULL, 8, 42));
}

/* ---- Network JSON envelope ---- */

static json_object *find(json_object *o, const char *k) {
   json_object *v = NULL;
   json_object_object_get_ex(o, k, &v);
   return v;
}

void test_build_network_json_shape(void) {
   network_status_t st;
   memset(&st, 0, sizeof(st));
   strcpy(st.hostname, "oasis-jetson");

   st.iface_count = 1;
   strcpy(st.ifaces[0].name, "usb0");
   strcpy(st.ifaces[0].driver, "rndis_host");
   st.ifaces[0].kind = NET_KIND_CELLULAR;
   strcpy(st.ifaces[0].operstate, "unknown");
   st.ifaces[0].up = true;
   st.ifaces[0].carrier = true;
   st.ifaces[0].mtu = 1420;
   st.ifaces[0].speed_mbps = -1;
   strcpy(st.ifaces[0].ipv4[0], "192.168.225.40");
   st.ifaces[0].ipv4_count = 1;
   st.ifaces[0].rx_bytes = 987654;
   st.ifaces[0].tx_bytes = 456789;

   st.route_count = 1;
   strcpy(st.routes[0].iface, "usb0");
   strcpy(st.routes[0].gateway, "192.168.225.1");
   st.routes[0].metric = 20100;
   st.routes[0].family = NET_FAMILY_IPV4;

   st.reach_count = 1;
   strcpy(st.reach[0].iface, "usb0");
   strcpy(st.reach[0].gateway, "192.168.225.1");
   st.reach[0].reachable = true;
   st.reach[0].rtt_ms = 2.2;
   st.reach[0].fail_streak = 0;
   st.reach[0].bound = true;

   json_object *root = build_network_json(&st);
   TEST_ASSERT_NOT_NULL(root);

   TEST_ASSERT_EQUAL_STRING("Network", json_object_get_string(find(root, "type")));
   TEST_ASSERT_EQUAL_STRING("stat", json_object_get_string(find(root, "device")));
   TEST_ASSERT_EQUAL_STRING("oasis-jetson", json_object_get_string(find(root, "hostname")));

   json_object *ifaces = find(root, "interfaces");
   TEST_ASSERT_EQUAL_INT(1, json_object_array_length(ifaces));
   json_object *i0 = json_object_array_get_idx(ifaces, 0);
   TEST_ASSERT_EQUAL_STRING("cellular", json_object_get_string(find(i0, "kind")));
   TEST_ASSERT_EQUAL_STRING("unknown", json_object_get_string(find(i0, "state")));
   TEST_ASSERT_TRUE(json_object_get_boolean(find(i0, "up")));
   TEST_ASSERT_TRUE(json_object_get_boolean(find(i0, "carrier")));
   TEST_ASSERT_EQUAL_INT(1, json_object_array_length(find(i0, "ipv4")));

   json_object *routes = find(root, "default_routes");
   TEST_ASSERT_EQUAL_INT(1, json_object_array_length(routes));
   TEST_ASSERT_EQUAL_STRING("ipv4", json_object_get_string(
                                        find(json_object_array_get_idx(routes, 0), "family")));

   json_object *reach = find(root, "reachability");
   TEST_ASSERT_EQUAL_INT(1, json_object_array_length(reach));
   json_object *r0 = json_object_array_get_idx(reach, 0);
   TEST_ASSERT_EQUAL_STRING("gateway", json_object_get_string(find(r0, "target_kind")));
   TEST_ASSERT_TRUE(json_object_get_boolean(find(r0, "reachable")));
   /* rtt_ms present only when reachable. */
   TEST_ASSERT_NOT_NULL(find(r0, "rtt_ms"));

   json_object_put(root);
}

void test_build_network_json_unreachable_omits_rtt(void) {
   network_status_t st;
   memset(&st, 0, sizeof(st));
   st.reach_count = 1;
   strcpy(st.reach[0].gateway, "10.0.0.1");
   strcpy(st.reach[0].iface, "eth0");
   st.reach[0].reachable = false;
   st.reach[0].fail_streak = 3;

   json_object *root = build_network_json(&st);
   json_object *r0 = json_object_array_get_idx(find(root, "reachability"), 0);
   TEST_ASSERT_FALSE(json_object_get_boolean(find(r0, "reachable")));
   TEST_ASSERT_NULL(find(r0, "rtt_ms")); /* omitted, single encoding */
   TEST_ASSERT_EQUAL_INT(3, json_object_get_int(find(r0, "fail_streak")));
   json_object_put(root);
}

/* Recursively assert every key in `expected` exists in `actual` (nested objects
 * and the first element of parallel arrays). Pins build_network_json against the
 * frozen wire contract so a field rename/removal is caught in STAT's own tests. */
static void assert_keys_subset(json_object *expected, json_object *actual) {
   json_object_object_foreach(expected, key, eval) {
      json_object *aval = NULL;
      TEST_ASSERT_TRUE_MESSAGE(json_object_object_get_ex(actual, key, &aval), key);
      enum json_type et = json_object_get_type(eval);
      enum json_type at = json_object_get_type(aval);
      if (et == json_type_object && at == json_type_object) {
         assert_keys_subset(eval, aval);
      } else if (et == json_type_array && at == json_type_array &&
                 json_object_array_length(eval) > 0 && json_object_array_length(aval) > 0) {
         json_object *e0 = json_object_array_get_idx(eval, 0);
         json_object *a0 = json_object_array_get_idx(aval, 0);
         if (json_object_get_type(e0) == json_type_object &&
             json_object_get_type(a0) == json_type_object) {
            assert_keys_subset(e0, a0);
         }
      }
   }
}

void test_build_network_json_matches_fixture(void) {
   json_object *fix = json_object_from_file(NETWORK_FIXTURE);
   TEST_ASSERT_NOT_NULL_MESSAGE(fix, "load " NETWORK_FIXTURE);

   network_status_t st;
   memset(&st, 0, sizeof(st));
   strcpy(st.hostname, "oasis-jetson");
   st.probe_available = true;

   st.iface_count = 1;
   strcpy(st.ifaces[0].name, "usb0");
   strcpy(st.ifaces[0].driver, "rndis_host");
   st.ifaces[0].kind = NET_KIND_CELLULAR;
   strcpy(st.ifaces[0].operstate, "unknown");
   st.ifaces[0].up = true;
   st.ifaces[0].carrier = true;
   st.ifaces[0].speed_mbps = -1;
   strcpy(st.ifaces[0].ipv4[0], "192.168.225.40");
   st.ifaces[0].ipv4_count = 1;
   strcpy(st.ifaces[0].ipv6[0], "2607:fb90:7c1c:ccc7:6627:9487:834b:af2b");
   st.ifaces[0].ipv6_count = 1;

   st.route_count = 1;
   strcpy(st.routes[0].iface, "usb0");
   strcpy(st.routes[0].gateway, "192.168.225.1");
   st.routes[0].metric = 20100;
   st.routes[0].family = NET_FAMILY_IPV4;

   st.reach_count = 1;
   strcpy(st.reach[0].iface, "usb0");
   strcpy(st.reach[0].gateway, "192.168.225.1");
   st.reach[0].reachable = true;
   st.reach[0].rtt_ms = 2.2;
   st.reach[0].bound = true;

   json_object *built = build_network_json(&st);
   TEST_ASSERT_NOT_NULL(built);
   assert_keys_subset(fix, built);

   json_object_put(built);
   json_object_put(fix);
}

int main(void) {
   UNITY_BEGIN();
   RUN_TEST(test_ipv4_routes_dual_default);
   RUN_TEST(test_ipv4_routes_malformed_lines_skipped);
   RUN_TEST(test_ipv4_routes_truncation);
   RUN_TEST(test_ipv6_routes_default_only);
   RUN_TEST(test_inet6_addrs_stable_only);
   RUN_TEST(test_classify_kind);
   RUN_TEST(test_icmp_reply_ok);
   RUN_TEST(test_build_network_json_shape);
   RUN_TEST(test_build_network_json_unreachable_omits_rtt);
   RUN_TEST(test_build_network_json_matches_fixture);
   return UNITY_END();
}
