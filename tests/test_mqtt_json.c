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
 * Unit tests for mqtt_publisher.c JSON envelope construction. Tests the
 * pure build_battery_json() / build_daly_bms_json() builders without a
 * broker connection.
 */

#include <json-c/json.h>
#include <stdbool.h>
#include <string.h>

#include "battery_model.h"
#include "daly_bms.h"
#include "ina238.h"
#include "mqtt_publisher_internal.h"
#include "unity.h"

static struct json_object *g_root = NULL;

void setUp(void) {
   g_root = NULL;
}

void tearDown(void) {
   if (g_root) {
      json_object_put(g_root);
      g_root = NULL;
   }
}

/* Helpers */

static ina238_measurements_t make_measurements(float voltage, float current) {
   ina238_measurements_t m = { 0 };
   m.bus_voltage = voltage;
   m.current = current;
   m.power = voltage * current;
   m.temperature = 30.0f;
   m.valid = true;
   return m;
}

static battery_config_t make_liion_config(void) {
   battery_config_t cfg = { 0 };
   cfg.chemistry = BATT_CHEMISTRY_LIION;
   cfg.cells_series = 5;
   cfg.cells_parallel = 1;
   cfg.min_voltage = 14.25f;
   cfg.max_voltage = 20.85f;
   cfg.nominal_voltage = 18.5f;
   cfg.capacity_mah = 5000.0f;
   strncpy(cfg.name, "test-pack", BATTERY_NAME_MAX_LENGTH - 1);
   return cfg;
}

static int json_get_int(struct json_object *root, const char *key) {
   struct json_object *field;
   TEST_ASSERT_TRUE_MESSAGE(json_object_object_get_ex(root, key, &field), key);
   return json_object_get_int(field);
}

static const char *json_get_string(struct json_object *root, const char *key) {
   struct json_object *field;
   TEST_ASSERT_TRUE_MESSAGE(json_object_object_get_ex(root, key, &field), key);
   return json_object_get_string(field);
}

static double json_get_double(struct json_object *root, const char *key) {
   struct json_object *field;
   TEST_ASSERT_TRUE_MESSAGE(json_object_object_get_ex(root, key, &field), key);
   return json_object_get_double(field);
}

/* build_battery_json */

void test_battery_json_invalid_measurements_returns_null(void) {
   ina238_measurements_t m = { 0 };
   m.valid = false;
   g_root = build_battery_json(&m, 50.0f, NULL);
   TEST_ASSERT_NULL(g_root);
}

void test_battery_json_ocp_envelope_fields(void) {
   ina238_measurements_t m = make_measurements(17.0f, 2.5f);
   g_root = build_battery_json(&m, 60.0f, NULL);
   TEST_ASSERT_NOT_NULL(g_root);
   TEST_ASSERT_EQUAL_STRING("stat", json_get_string(g_root, "device"));
   TEST_ASSERT_EQUAL_STRING("telemetry", json_get_string(g_root, "msg_type"));
   TEST_ASSERT_EQUAL_STRING("Battery", json_get_string(g_root, "type"));
   TEST_ASSERT_EQUAL_STRING("INA238", json_get_string(g_root, "sensor"));
   struct json_object *ts;
   TEST_ASSERT_TRUE(json_object_object_get_ex(g_root, "timestamp", &ts));
   TEST_ASSERT_TRUE(json_object_get_int64(ts) > 0);
}

void test_battery_json_status_critical_at_10pct(void) {
   ina238_measurements_t m = make_measurements(14.5f, 2.0f);
   g_root = build_battery_json(&m, 5.0f, NULL);
   TEST_ASSERT_EQUAL_STRING("CRITICAL", json_get_string(g_root, "battery_status"));
}

void test_battery_json_status_warning_between_10_and_20pct(void) {
   ina238_measurements_t m = make_measurements(16.0f, 2.0f);
   g_root = build_battery_json(&m, 15.0f, NULL);
   TEST_ASSERT_EQUAL_STRING("WARNING", json_get_string(g_root, "battery_status"));
}

void test_battery_json_status_normal_above_20pct(void) {
   ina238_measurements_t m = make_measurements(18.5f, 2.0f);
   g_root = build_battery_json(&m, 75.0f, NULL);
   TEST_ASSERT_EQUAL_STRING("NORMAL", json_get_string(g_root, "battery_status"));
}

void test_battery_json_measurement_fields_match(void) {
   ina238_measurements_t m = make_measurements(18.5f, 2.5f);
   g_root = build_battery_json(&m, 60.0f, NULL);
   TEST_ASSERT_DOUBLE_WITHIN(0.01, 18.5, json_get_double(g_root, "voltage"));
   TEST_ASSERT_DOUBLE_WITHIN(0.01, 2.5, json_get_double(g_root, "current"));
   TEST_ASSERT_DOUBLE_WITHIN(0.01, 46.25, json_get_double(g_root, "power"));
   TEST_ASSERT_DOUBLE_WITHIN(0.01, 60.0, json_get_double(g_root, "battery_level"));
}

void test_battery_json_null_battery_omits_detail_fields(void) {
   ina238_measurements_t m = make_measurements(18.0f, 2.0f);
   g_root = build_battery_json(&m, 50.0f, NULL);
   struct json_object *f;
   TEST_ASSERT_FALSE(json_object_object_get_ex(g_root, "battery_chemistry", &f));
   TEST_ASSERT_FALSE(json_object_object_get_ex(g_root, "battery_capacity_mah", &f));
   TEST_ASSERT_FALSE(json_object_object_get_ex(g_root, "time_remaining_min", &f));
}

void test_battery_json_with_battery_adds_detail_fields(void) {
   ina238_measurements_t m = make_measurements(18.0f, 2.0f);
   battery_config_t cfg = make_liion_config();
   g_root = build_battery_json(&m, 50.0f, &cfg);
   TEST_ASSERT_EQUAL_STRING("Li-ion", json_get_string(g_root, "battery_chemistry"));
   TEST_ASSERT_EQUAL_INT(5, json_get_int(g_root, "battery_cells"));
   TEST_ASSERT_DOUBLE_WITHIN(0.01, 5000.0, json_get_double(g_root, "battery_capacity_mah"));
   struct json_object *fmt;
   TEST_ASSERT_TRUE(json_object_object_get_ex(g_root, "time_remaining_fmt", &fmt));
   const char *fmt_str = json_object_get_string(fmt);
   /* Expected format H:MM (one or more digits, colon, exactly two digits) */
   TEST_ASSERT_NOT_NULL(strchr(fmt_str, ':'));
}

/* build_daly_bms_json */

/* Fill-by-pointer to avoid a ~2.6 KB struct copy per test invocation. */
static void fill_daly_device(daly_device_t *dev, int cell_count, int fault_count) {
   memset(dev, 0, sizeof(*dev));
   dev->initialized = true;
   dev->data.valid = true;
   dev->data.status.cell_count = cell_count;
   dev->data.status.ntc_count = 2;
   dev->data.pack.v_total_v = 48.0f;
   dev->data.pack.current_a = -5.0f;
   dev->data.pack.soc_pct = 75.0f;
   dev->data.extremes.vmax_v = 4.02f;
   dev->data.extremes.vmax_cell = 1;
   dev->data.extremes.vmin_v = 3.98f;
   dev->data.extremes.vmin_cell = 12;
   dev->data.mos.charge_mos = true;
   dev->data.mos.discharge_mos = true;
   dev->data.mos.life_cycles = 123;
   dev->data.mos.remain_capacity_mah = 7500;
   dev->data.temps.tmax_c = 32.0f;
   dev->data.temps.tmax_idx = 1;
   dev->data.temps.tmin_c = 28.0f;
   dev->data.temps.tmin_idx = 2;
   dev->data.temps.ntc_count = 2;
   dev->data.temps.sensors_c[0] = 32.0f;
   dev->data.temps.sensors_c[1] = 28.0f;
   for (int i = 0; i < cell_count; i++) {
      dev->data.cell_mv[i] = 4000 + i; /* small spread */
      dev->data.balance[i] = false;
   }
   dev->data.fault_count = fault_count;
   for (int i = 0; i < fault_count; i++) {
      snprintf(dev->data.faults[i], sizeof(dev->data.faults[i]), "Fault %d", i);
   }
}

void test_daly_json_invalid_device_returns_null(void) {
   daly_device_t dev = { 0 };
   dev.initialized = false;
   g_root = build_daly_bms_json(&dev, NULL);
   TEST_ASSERT_NULL(g_root);
}

void test_daly_json_ocp_envelope(void) {
   daly_device_t dev;
   fill_daly_device(&dev, 16, 0);
   g_root = build_daly_bms_json(&dev, NULL);
   TEST_ASSERT_NOT_NULL(g_root);
   TEST_ASSERT_EQUAL_STRING("stat", json_get_string(g_root, "device"));
   TEST_ASSERT_EQUAL_STRING("telemetry", json_get_string(g_root, "msg_type"));
   TEST_ASSERT_EQUAL_STRING("Battery", json_get_string(g_root, "type"));
   TEST_ASSERT_EQUAL_STRING("DalyBMS", json_get_string(g_root, "sensor"));
}

void test_daly_json_cells_array_size_matches_cell_count(void) {
   daly_device_t dev;
   fill_daly_device(&dev, 13, 0);
   g_root = build_daly_bms_json(&dev, NULL);
   struct json_object *cells;
   TEST_ASSERT_TRUE(json_object_object_get_ex(g_root, "cells", &cells));
   TEST_ASSERT_EQUAL_INT(13, json_object_array_length(cells));
}

void test_daly_json_faults_array_matches_fault_count(void) {
   daly_device_t dev;
   fill_daly_device(&dev, 4, 3);
   g_root = build_daly_bms_json(&dev, NULL);
   struct json_object *faults;
   TEST_ASSERT_TRUE(json_object_object_get_ex(g_root, "faults", &faults));
   TEST_ASSERT_EQUAL_INT(3, json_object_array_length(faults));
}

void test_daly_json_derived_state_discharging(void) {
   daly_device_t dev;
   fill_daly_device(&dev, 4, 0);
   dev.data.pack.current_a = -5.0f; /* discharging */
   g_root = build_daly_bms_json(&dev, NULL);
   TEST_ASSERT_EQUAL_STRING("discharging", json_get_string(g_root, "charging_state"));
}

void test_daly_json_derived_state_charging(void) {
   daly_device_t dev;
   fill_daly_device(&dev, 4, 0);
   dev.data.pack.current_a = +5.0f;
   g_root = build_daly_bms_json(&dev, NULL);
   TEST_ASSERT_EQUAL_STRING("charging", json_get_string(g_root, "charging_state"));
}

void test_daly_json_pack_fields_match(void) {
   daly_device_t dev;
   fill_daly_device(&dev, 4, 0);
   g_root = build_daly_bms_json(&dev, NULL);
   TEST_ASSERT_DOUBLE_WITHIN(0.01, 48.0, json_get_double(g_root, "voltage"));
   TEST_ASSERT_DOUBLE_WITHIN(0.01, -5.0, json_get_double(g_root, "current"));
   TEST_ASSERT_DOUBLE_WITHIN(0.01, 75.0, json_get_double(g_root, "battery_level"));
   TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.04, json_get_double(g_root, "vdelta"));
}

int main(void) {
   UNITY_BEGIN();

   RUN_TEST(test_battery_json_invalid_measurements_returns_null);
   RUN_TEST(test_battery_json_ocp_envelope_fields);
   RUN_TEST(test_battery_json_status_critical_at_10pct);
   RUN_TEST(test_battery_json_status_warning_between_10_and_20pct);
   RUN_TEST(test_battery_json_status_normal_above_20pct);
   RUN_TEST(test_battery_json_measurement_fields_match);
   RUN_TEST(test_battery_json_null_battery_omits_detail_fields);
   RUN_TEST(test_battery_json_with_battery_adds_detail_fields);

   RUN_TEST(test_daly_json_invalid_device_returns_null);
   RUN_TEST(test_daly_json_ocp_envelope);
   RUN_TEST(test_daly_json_cells_array_size_matches_cell_count);
   RUN_TEST(test_daly_json_faults_array_matches_fault_count);
   RUN_TEST(test_daly_json_derived_state_discharging);
   RUN_TEST(test_daly_json_derived_state_charging);
   RUN_TEST(test_daly_json_pack_fields_match);

   return UNITY_END();
}
