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
 * Unit tests for battery modeling: SOC curves, chemistry string conversion,
 * voltage clamping. Pure-logic tests with no hardware dependency.
 */

#include <string.h>

#include "battery_model.h"
#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

/* Helpers */

static battery_config_t make_liion_5s(void) {
   battery_config_t cfg;
   memset(&cfg, 0, sizeof(cfg));
   cfg.chemistry = BATT_CHEMISTRY_LIION;
   cfg.cells_series = 5;
   cfg.cells_parallel = 1;
   cfg.min_voltage = 14.25f; /* 5 × 2.85V */
   cfg.max_voltage = 20.85f; /* 5 × 4.17V */
   cfg.nominal_voltage = 18.5f;
   cfg.capacity_mah = 5000.0f;
   return cfg;
}

static battery_config_t make_lipo_3s(void) {
   battery_config_t cfg;
   memset(&cfg, 0, sizeof(cfg));
   cfg.chemistry = BATT_CHEMISTRY_LIPO;
   cfg.cells_series = 3;
   cfg.cells_parallel = 1;
   cfg.min_voltage = 9.45f;  /* 3 × 3.15V */
   cfg.max_voltage = 12.51f; /* 3 × 4.17V */
   cfg.capacity_mah = 2200.0f;
   return cfg;
}

static battery_config_t make_lifepo4_4s(void) {
   battery_config_t cfg;
   memset(&cfg, 0, sizeof(cfg));
   cfg.chemistry = BATT_CHEMISTRY_LIFEPO4;
   cfg.cells_series = 4;
   cfg.cells_parallel = 1;
   cfg.min_voltage = 9.72f;  /* 4 × 2.43V */
   cfg.max_voltage = 13.52f; /* 4 × 3.38V */
   cfg.capacity_mah = 10000.0f;
   return cfg;
}

static battery_config_t make_unknown_linear(void) {
   battery_config_t cfg;
   memset(&cfg, 0, sizeof(cfg));
   cfg.chemistry = BATT_CHEMISTRY_UNKNOWN;
   cfg.cells_series = 0; /* triggers linear fallback */
   cfg.cells_parallel = 1;
   cfg.min_voltage = 10.0f;
   cfg.max_voltage = 14.0f;
   cfg.capacity_mah = 2000.0f;
   return cfg;
}

/* SOC curve tests — boundary values match discharge_point_t tables in battery_model.c */

void test_liion_percentage_empty(void) {
   battery_config_t cfg = make_liion_5s();
   float pct = battery_calculate_percentage(cfg.min_voltage, &cfg);
   TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, pct);
}

void test_liion_percentage_full(void) {
   battery_config_t cfg = make_liion_5s();
   float pct = battery_calculate_percentage(cfg.max_voltage, &cfg);
   TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, pct);
}

void test_liion_percentage_midrange(void) {
   battery_config_t cfg = make_liion_5s();
   /* 5 × 3.68V = 18.4V is the 50% point on the Li-ion curve */
   float pct = battery_calculate_percentage(18.4f, &cfg);
   TEST_ASSERT_FLOAT_WITHIN(2.0f, 50.0f, pct);
}

void test_liion_voltage_below_min_clamps_to_zero(void) {
   battery_config_t cfg = make_liion_5s();
   float pct = battery_calculate_percentage(5.0f, &cfg);
   TEST_ASSERT_EQUAL_FLOAT(0.0f, pct);
}

void test_liion_voltage_above_max_clamps_to_100(void) {
   battery_config_t cfg = make_liion_5s();
   float pct = battery_calculate_percentage(30.0f, &cfg);
   TEST_ASSERT_EQUAL_FLOAT(100.0f, pct);
}

void test_lipo_percentage_full(void) {
   battery_config_t cfg = make_lipo_3s();
   float pct = battery_calculate_percentage(cfg.max_voltage, &cfg);
   TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, pct);
}

void test_lipo_percentage_midrange(void) {
   battery_config_t cfg = make_lipo_3s();
   /* 3 × 3.73V = 11.19V is the 50% point on the LiPo curve */
   float pct = battery_calculate_percentage(11.19f, &cfg);
   TEST_ASSERT_FLOAT_WITHIN(2.0f, 50.0f, pct);
}

void test_lifepo4_percentage_full(void) {
   battery_config_t cfg = make_lifepo4_4s();
   float pct = battery_calculate_percentage(cfg.max_voltage, &cfg);
   TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, pct);
}

void test_lifepo4_percentage_empty(void) {
   battery_config_t cfg = make_lifepo4_4s();
   float pct = battery_calculate_percentage(cfg.min_voltage, &cfg);
   TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, pct);
}

/* NiMH and Lead-Acid fall into the switch default branch (linear 3.0V-4.2V per cell).
 * Validate they don't crash and produce values in a sane range. */

void test_nimh_uses_linear_fallback(void) {
   battery_config_t cfg = make_liion_5s();
   cfg.chemistry = BATT_CHEMISTRY_NIMH;
   /* Fallback formula: soc = (cell_V - 3.0) / (4.2 - 3.0), midpoint ≈ 3.6V/cell */
   float pct = battery_calculate_percentage(5.0f * 3.6f, &cfg);
   TEST_ASSERT_TRUE(pct >= 0.0f && pct <= 100.0f);
   /* Midpoint of fallback range should be ~50% */
   TEST_ASSERT_FLOAT_WITHIN(5.0f, 50.0f, pct);
}

void test_lead_acid_uses_linear_fallback(void) {
   battery_config_t cfg = make_liion_5s();
   cfg.chemistry = BATT_CHEMISTRY_LEAD_ACID;
   float pct = battery_calculate_percentage(cfg.max_voltage, &cfg);
   TEST_ASSERT_TRUE(pct >= 0.0f && pct <= 100.0f);
}

/* Unknown chemistry + zero cells_series → pack-level linear model */

void test_unknown_chemistry_linear_midpoint(void) {
   battery_config_t cfg = make_unknown_linear();
   /* Midpoint of 10V-14V range */
   float pct = battery_calculate_percentage(12.0f, &cfg);
   TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, pct);
}

void test_unknown_chemistry_clamps_low(void) {
   battery_config_t cfg = make_unknown_linear();
   float pct = battery_calculate_percentage(5.0f, &cfg);
   TEST_ASSERT_EQUAL_FLOAT(0.0f, pct);
}

void test_unknown_chemistry_clamps_high(void) {
   battery_config_t cfg = make_unknown_linear();
   float pct = battery_calculate_percentage(20.0f, &cfg);
   TEST_ASSERT_EQUAL_FLOAT(100.0f, pct);
}

void test_null_config_returns_zero(void) {
   float pct = battery_calculate_percentage(12.0f, NULL);
   TEST_ASSERT_EQUAL_FLOAT(0.0f, pct);
}

/* Chemistry string conversion */

void test_chemistry_to_string_all_values(void) {
   TEST_ASSERT_EQUAL_STRING("Li-ion", battery_chemistry_to_string(BATT_CHEMISTRY_LIION));
   TEST_ASSERT_EQUAL_STRING("LiPo", battery_chemistry_to_string(BATT_CHEMISTRY_LIPO));
   TEST_ASSERT_EQUAL_STRING("LiFePO4", battery_chemistry_to_string(BATT_CHEMISTRY_LIFEPO4));
   TEST_ASSERT_EQUAL_STRING("NiMH", battery_chemistry_to_string(BATT_CHEMISTRY_NIMH));
   TEST_ASSERT_EQUAL_STRING("Lead-Acid", battery_chemistry_to_string(BATT_CHEMISTRY_LEAD_ACID));
   TEST_ASSERT_EQUAL_STRING("Unknown", battery_chemistry_to_string(BATT_CHEMISTRY_UNKNOWN));
}

void test_chemistry_from_string_round_trip(void) {
   /* Known values should round-trip through to_string → from_string */
   const battery_chemistry_t vals[] = { BATT_CHEMISTRY_LIION, BATT_CHEMISTRY_LIPO,
                                        BATT_CHEMISTRY_LIFEPO4, BATT_CHEMISTRY_NIMH,
                                        BATT_CHEMISTRY_LEAD_ACID };
   for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
      const char *name = battery_chemistry_to_string(vals[i]);
      battery_chemistry_t decoded = battery_chemistry_from_string(name);
      TEST_ASSERT_EQUAL_INT(vals[i], decoded);
   }
}

void test_chemistry_from_string_null_returns_unknown(void) {
   TEST_ASSERT_EQUAL_INT(BATT_CHEMISTRY_UNKNOWN, battery_chemistry_from_string(NULL));
}

void test_chemistry_from_string_garbage_returns_unknown(void) {
   TEST_ASSERT_EQUAL_INT(BATT_CHEMISTRY_UNKNOWN, battery_chemistry_from_string("not-a-chemistry"));
}

int main(void) {
   UNITY_BEGIN();

   RUN_TEST(test_liion_percentage_empty);
   RUN_TEST(test_liion_percentage_full);
   RUN_TEST(test_liion_percentage_midrange);
   RUN_TEST(test_liion_voltage_below_min_clamps_to_zero);
   RUN_TEST(test_liion_voltage_above_max_clamps_to_100);

   RUN_TEST(test_lipo_percentage_full);
   RUN_TEST(test_lipo_percentage_midrange);

   RUN_TEST(test_lifepo4_percentage_full);
   RUN_TEST(test_lifepo4_percentage_empty);

   RUN_TEST(test_nimh_uses_linear_fallback);
   RUN_TEST(test_lead_acid_uses_linear_fallback);

   RUN_TEST(test_unknown_chemistry_linear_midpoint);
   RUN_TEST(test_unknown_chemistry_clamps_low);
   RUN_TEST(test_unknown_chemistry_clamps_high);
   RUN_TEST(test_null_config_returns_zero);

   RUN_TEST(test_chemistry_to_string_all_values);
   RUN_TEST(test_chemistry_from_string_round_trip);
   RUN_TEST(test_chemistry_from_string_null_returns_unknown);
   RUN_TEST(test_chemistry_from_string_garbage_returns_unknown);

   return UNITY_END();
}
