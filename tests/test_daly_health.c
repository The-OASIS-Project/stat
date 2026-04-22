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
 * Unit tests for Daly BMS health analysis and fault categorization. Uses
 * hand-built daly_device_t fixtures — no serial hardware.
 */

#include <stdbool.h>
#include <string.h>

#include "daly_bms.h"
#include "unity.h"

/* Thresholds matching DALY_CELL_WARNING_THRESHOLD_MV / DALY_CELL_CRITICAL_THRESHOLD_MV */
#define WARN_MV 70
#define CRIT_MV 120

static daly_device_t g_dev;
static daly_pack_health_t g_health;

void setUp(void) {
   memset(&g_dev, 0, sizeof(g_dev));
   memset(&g_health, 0, sizeof(g_health));
   g_dev.initialized = true;
   g_dev.data.valid = true;
}

void tearDown(void) {
   daly_bms_free_health(&g_health);
}

/* Fixture helper: populate an N-cell pack with all cells at the given millivolts.
 * Sets up pack.v_total_v, extremes, status.cell_count accordingly. */
static void fixture_balanced_pack(int cell_count, int cell_mv) {
   g_dev.data.status.cell_count = cell_count;
   for (int i = 0; i < cell_count; i++) {
      g_dev.data.cell_mv[i] = cell_mv;
      g_dev.data.balance[i] = false;
   }
   g_dev.data.extremes.vmax_v = cell_mv / 1000.0f;
   g_dev.data.extremes.vmax_cell = 1;
   g_dev.data.extremes.vmin_v = cell_mv / 1000.0f;
   g_dev.data.extremes.vmin_cell = 1;
   g_dev.data.pack.v_total_v = (cell_count * cell_mv) / 1000.0f;
   g_dev.data.fault_count = 0;
}

/* analyze_health */

void test_health_all_cells_balanced_is_normal(void) {
   fixture_balanced_pack(4, 3700);
   int status = daly_bms_analyze_health(&g_dev, &g_health, WARN_MV, CRIT_MV);
   TEST_ASSERT_EQUAL_INT(DALY_HEALTH_NORMAL, status);
   TEST_ASSERT_EQUAL_INT(0, g_health.problem_cell_count);
}

void test_health_single_cell_warning_deviation(void) {
   fixture_balanced_pack(4, 3700);
   /* Bump cell 2 by 80 mV → average shifts to ~3720, cell 2 deviates ~60mV.
    * Safer to make the deviation unambiguous: set one cell 100mV above the others. */
   g_dev.data.cell_mv[1] = 3800; /* 100 mV above others */
   g_dev.data.extremes.vmax_v = 3.800f;
   g_dev.data.extremes.vmax_cell = 2;
   /* vavg ≈ (3700*3 + 3800)/4000 = 3725; cell 2 deviation ≈ 75 mV (warning, not critical) */
   int status = daly_bms_analyze_health(&g_dev, &g_health, WARN_MV, CRIT_MV);
   TEST_ASSERT_EQUAL_INT(DALY_HEALTH_WARNING, status);
   TEST_ASSERT_TRUE(g_health.problem_cell_count >= 1);
}

void test_health_single_cell_critical_deviation(void) {
   fixture_balanced_pack(4, 3700);
   g_dev.data.cell_mv[2] = 3900; /* 200 mV above others */
   g_dev.data.extremes.vmax_v = 3.900f;
   g_dev.data.extremes.vmax_cell = 3;
   /* vavg = 3750; cell 3 deviation = 150 mV (above 120 mV critical) */
   int status = daly_bms_analyze_health(&g_dev, &g_health, WARN_MV, CRIT_MV);
   TEST_ASSERT_EQUAL_INT(DALY_HEALTH_CRITICAL, status);
   TEST_ASSERT_TRUE(g_health.problem_cell_count >= 1);
}

void test_health_cell_below_2v_flagged_as_warning(void) {
   fixture_balanced_pack(4, 3700);
   g_dev.data.cell_mv[0] = 500; /* 0.5V — well below the 2V sanity threshold */
   /* Per code: cells ≤2V are treated as warnings (BMS config issue), not critical. */
   int status = daly_bms_analyze_health(&g_dev, &g_health, WARN_MV, CRIT_MV);
   /* Other cells still at 3700. vavg = 3700 (invalid cell excluded from avg).
    * The low cell is tagged WARNING individually. Overall should be at least WARNING. */
   TEST_ASSERT_TRUE(status == DALY_HEALTH_WARNING || status == DALY_HEALTH_CRITICAL);
   TEST_ASSERT_TRUE(g_health.problem_cell_count >= 1);
   TEST_ASSERT_EQUAL_INT(DALY_HEALTH_WARNING, g_health.cells[0].status);
}

void test_health_faults_elevate_to_warning(void) {
   fixture_balanced_pack(4, 3700);
   snprintf(g_dev.data.faults[0], sizeof(g_dev.data.faults[0]), "%s", "Some fault");
   g_dev.data.fault_count = 1;
   int status = daly_bms_analyze_health(&g_dev, &g_health, WARN_MV, CRIT_MV);
   TEST_ASSERT_EQUAL_INT(DALY_HEALTH_WARNING, status);
}

void test_health_null_device_safe(void) {
   int status = daly_bms_analyze_health(NULL, &g_health, WARN_MV, CRIT_MV);
   TEST_ASSERT_EQUAL_INT(DALY_HEALTH_NORMAL, status);
}

void test_health_null_health_safe(void) {
   fixture_balanced_pack(4, 3700);
   int status = daly_bms_analyze_health(&g_dev, NULL, WARN_MV, CRIT_MV);
   TEST_ASSERT_EQUAL_INT(DALY_HEALTH_NORMAL, status);
}

/* categorize_faults */

void test_categorize_empty_faults(void) {
   g_dev.data.fault_count = 0;
   daly_fault_summary_t summary;
   TEST_ASSERT_EQUAL_INT(0, daly_bms_categorize_faults(&g_dev, &summary));
   TEST_ASSERT_EQUAL_INT(0, summary.critical_count);
   TEST_ASSERT_EQUAL_INT(0, summary.warning_count);
   TEST_ASSERT_EQUAL_INT(0, summary.info_count);
}

void test_categorize_l2_fault_is_critical(void) {
   snprintf(g_dev.data.faults[0], sizeof(g_dev.data.faults[0]), "%s", "Cell volt high L2");
   g_dev.data.fault_count = 1;
   daly_fault_summary_t summary;
   TEST_ASSERT_EQUAL_INT(0, daly_bms_categorize_faults(&g_dev, &summary));
   TEST_ASSERT_EQUAL_INT(1, summary.critical_count);
   TEST_ASSERT_EQUAL_STRING("Cell volt high L2", summary.critical_faults[0]);
}

void test_categorize_l1_fault_is_warning(void) {
   snprintf(g_dev.data.faults[0], sizeof(g_dev.data.faults[0]), "%s", "Cell volt high L1");
   g_dev.data.fault_count = 1;
   daly_fault_summary_t summary;
   TEST_ASSERT_EQUAL_INT(0, daly_bms_categorize_faults(&g_dev, &summary));
   TEST_ASSERT_EQUAL_INT(1, summary.warning_count);
   TEST_ASSERT_EQUAL_STRING("Cell volt high L1", summary.warning_faults[0]);
}

void test_categorize_short_circuit_is_critical(void) {
   snprintf(g_dev.data.faults[0], sizeof(g_dev.data.faults[0]), "%s",
            "Short circuit protect fault");
   g_dev.data.fault_count = 1;
   daly_fault_summary_t summary;
   TEST_ASSERT_EQUAL_INT(0, daly_bms_categorize_faults(&g_dev, &summary));
   TEST_ASSERT_EQUAL_INT(1, summary.critical_count);
}

void test_categorize_failure_keyword_is_critical(void) {
   snprintf(g_dev.data.faults[0], sizeof(g_dev.data.faults[0]), "%s", "Temp sensor failure");
   g_dev.data.fault_count = 1;
   daly_fault_summary_t summary;
   TEST_ASSERT_EQUAL_INT(0, daly_bms_categorize_faults(&g_dev, &summary));
   TEST_ASSERT_EQUAL_INT(1, summary.critical_count);
}

void test_categorize_mixed_severities(void) {
   snprintf(g_dev.data.faults[0], sizeof(g_dev.data.faults[0]), "%s", "Cell volt low L2");
   snprintf(g_dev.data.faults[1], sizeof(g_dev.data.faults[1]), "%s", "SOC low L1");
   snprintf(g_dev.data.faults[2], sizeof(g_dev.data.faults[2]), "%s", "Charger adhesion");
   snprintf(g_dev.data.faults[3], sizeof(g_dev.data.faults[3]), "%s", "Miscellaneous thing");
   g_dev.data.fault_count = 4;

   daly_fault_summary_t summary;
   TEST_ASSERT_EQUAL_INT(0, daly_bms_categorize_faults(&g_dev, &summary));
   TEST_ASSERT_EQUAL_INT(1, summary.critical_count);
   TEST_ASSERT_EQUAL_INT(2, summary.warning_count); /* L1 + adhesion keyword */
   TEST_ASSERT_EQUAL_INT(1, summary.info_count);
}

void test_categorize_null_summary_returns_error(void) {
   g_dev.data.fault_count = 0;
   TEST_ASSERT_NOT_EQUAL(0, daly_bms_categorize_faults(&g_dev, NULL));
}

int main(void) {
   UNITY_BEGIN();

   RUN_TEST(test_health_all_cells_balanced_is_normal);
   RUN_TEST(test_health_single_cell_warning_deviation);
   RUN_TEST(test_health_single_cell_critical_deviation);
   RUN_TEST(test_health_cell_below_2v_flagged_as_warning);
   RUN_TEST(test_health_faults_elevate_to_warning);
   RUN_TEST(test_health_null_device_safe);
   RUN_TEST(test_health_null_health_safe);

   RUN_TEST(test_categorize_empty_faults);
   RUN_TEST(test_categorize_l2_fault_is_critical);
   RUN_TEST(test_categorize_l1_fault_is_warning);
   RUN_TEST(test_categorize_short_circuit_is_critical);
   RUN_TEST(test_categorize_failure_keyword_is_critical);
   RUN_TEST(test_categorize_mixed_severities);
   RUN_TEST(test_categorize_null_summary_returns_error);

   return UNITY_END();
}
