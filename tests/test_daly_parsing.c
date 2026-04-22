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
 * Unit tests for Daly BMS protocol frame parsing. Exercises the per-command
 * decoders (0x90, 0x91, 0x92, 0x93, 0x97, 0x98) plus checksum validation.
 * Operates entirely on in-memory byte arrays — no serial port required.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "daly_bms.h"
#include "daly_bms_internal.h"
#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

/* 0x90: Pack info — bytes[0..1] pack voltage (0.1V), bytes[2..3] mirror,
 * bytes[4..5] current with -30000 offset (0.1A), bytes[6..7] SOC (0.1%). */

void test_parse_0x90_pack_voltage_primary(void) {
   /* 456 (0x01C8) * 0.1 = 45.6 V */
   uint8_t data[8] = { 0x01, 0xC8, 0x00, 0x00, 0x75, 0x30, 0x03, 0xE8 };
   daly_pack_summary_t pack = { 0 };
   daly_parse_0x90(data, &pack);
   TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.6f, pack.v_total_v);
}

void test_parse_0x90_pack_voltage_fallback_to_cumulative(void) {
   /* bytes 0-1 = 0 → fallback to bytes 2-3 (120 * 0.1 = 12.0 V) */
   uint8_t data[8] = { 0x00, 0x00, 0x00, 0x78, 0x75, 0x30, 0x03, 0xE8 };
   daly_pack_summary_t pack = { 0 };
   daly_parse_0x90(data, &pack);
   TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f, pack.v_total_v);
   TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f, pack.v_total_cumulative_v);
}

void test_parse_0x90_zero_current(void) {
   /* Current raw = 30000 → (30000-30000)/10 = 0.0 A */
   uint8_t data[8] = { 0x01, 0x00, 0x00, 0x00, 0x75, 0x30, 0x00, 0x00 };
   daly_pack_summary_t pack = { 0 };
   daly_parse_0x90(data, &pack);
   TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, pack.current_a);
}

void test_parse_0x90_positive_current_charge(void) {
   /* Current raw = 30050 → (30050-30000)/10 = +5.0 A */
   uint8_t data[8] = { 0x01, 0x00, 0x00, 0x00, 0x75, 0x62, 0x00, 0x00 };
   daly_pack_summary_t pack = { 0 };
   daly_parse_0x90(data, &pack);
   TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, pack.current_a);
}

void test_parse_0x90_negative_current_discharge(void) {
   /* Current raw = 29900 → (29900-30000)/10 = -10.0 A */
   uint8_t data[8] = { 0x01, 0x00, 0x00, 0x00, 0x74, 0xCC, 0x00, 0x00 };
   daly_pack_summary_t pack = { 0 };
   daly_parse_0x90(data, &pack);
   TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f, pack.current_a);
}

void test_parse_0x90_soc_percentage(void) {
   /* SOC raw = 750 → 75.0% */
   uint8_t data[8] = { 0x01, 0x00, 0x00, 0x00, 0x75, 0x30, 0x02, 0xEE };
   daly_pack_summary_t pack = { 0 };
   daly_parse_0x90(data, &pack);
   TEST_ASSERT_FLOAT_WITHIN(0.05f, 75.0f, pack.soc_pct);
}

/* 0x91: Cell extremes — u16be(0..1) vmax_mV, byte 2 vmax cell#,
 * u16be(3..4) vmin_mV, byte 5 vmin cell#. */

void test_parse_0x91_extremes(void) {
   /* Vmax = 4150 mV (0x1036) on cell 3, Vmin = 3800 mV (0x0ED8) on cell 7 */
   uint8_t data[8] = { 0x10, 0x36, 0x03, 0x0E, 0xD8, 0x07, 0x00, 0x00 };
   daly_extremes_t ex = { 0 };
   daly_parse_0x91(data, &ex);
   TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.150f, ex.vmax_v);
   TEST_ASSERT_EQUAL_INT(3, ex.vmax_cell);
   TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.800f, ex.vmin_v);
   TEST_ASSERT_EQUAL_INT(7, ex.vmin_cell);
}

/* 0x92: Temperature — byte 0 tmax (offset -40), byte 1 tmax index,
 * byte 2 tmin (offset -40), byte 3 tmin index. */

void test_parse_0x92_temperature_offset(void) {
   /* tmax raw 65 → 25°C on sensor 1, tmin raw 55 → 15°C on sensor 2 */
   uint8_t data[8] = { 65, 1, 55, 2, 0, 0, 0, 0 };
   daly_temps_t t = { 0 };
   daly_parse_0x92(data, &t);
   TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, t.tmax_c);
   TEST_ASSERT_EQUAL_INT(1, t.tmax_idx);
   TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, t.tmin_c);
   TEST_ASSERT_EQUAL_INT(2, t.tmin_idx);
}

void test_parse_0x92_subzero_temperature(void) {
   /* Raw 20 → 20 - 40 = -20°C */
   uint8_t data[8] = { 20, 3, 20, 3, 0, 0, 0, 0 };
   daly_temps_t t = { 0 };
   daly_parse_0x92(data, &t);
   TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, t.tmax_c);
   TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, t.tmin_c);
}

/* 0x93: MOS status — state, chg_mos, dsg_mos, cycles, 32-bit remain capacity BE */

void test_parse_0x93_mos_flags(void) {
   /* state=1 (discharging), chg_mos=0, dsg_mos=1, cycles=42,
    * remain capacity = 0x01020304 = 16909060 mAh */
   uint8_t data[8] = { 0x01, 0x00, 0x01, 42, 0x01, 0x02, 0x03, 0x04 };
   daly_mos_caps_t mos = { 0 };
   daly_parse_0x93(data, &mos);
   TEST_ASSERT_EQUAL_INT(1, mos.state);
   TEST_ASSERT_FALSE(mos.charge_mos);
   TEST_ASSERT_TRUE(mos.discharge_mos);
   TEST_ASSERT_EQUAL_INT(42, mos.life_cycles);
   TEST_ASSERT_EQUAL_INT(0x01020304, mos.remain_capacity_mah);
}

void test_parse_0x93_both_mosfets_on(void) {
   uint8_t data[8] = { 2, 1, 1, 0, 0, 0, 0, 0 };
   daly_mos_caps_t mos = { 0 };
   daly_parse_0x93(data, &mos);
   TEST_ASSERT_TRUE(mos.charge_mos);
   TEST_ASSERT_TRUE(mos.discharge_mos);
}

/* 0x97: Cell balance — 8 bytes form a 64-bit field; LSB of byte 0 = cell 1. */

void test_parse_0x97_no_cells_balancing(void) {
   uint8_t data[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
   bool balance[DALY_MAX_CELLS];
   memset(balance, true, sizeof(balance)); /* seeded so we detect clears */
   daly_parse_0x97(data, 16, balance);
   for (int i = 0; i < 16; i++) {
      TEST_ASSERT_FALSE(balance[i]);
   }
}

void test_parse_0x97_cell_1_and_8_balancing(void) {
   /* Byte 0 = 0b10000001 → cells 1 (bit 0) and 8 (bit 7) balancing */
   uint8_t data[8] = { 0x81, 0, 0, 0, 0, 0, 0, 0 };
   bool balance[DALY_MAX_CELLS];
   memset(balance, false, sizeof(balance));
   daly_parse_0x97(data, 16, balance);
   TEST_ASSERT_TRUE(balance[0]);
   TEST_ASSERT_FALSE(balance[1]);
   TEST_ASSERT_TRUE(balance[7]);
   for (int i = 8; i < 16; i++) {
      TEST_ASSERT_FALSE(balance[i]);
   }
}

void test_parse_0x97_high_byte_cells(void) {
   /* Byte 3 bit 0 → cell at index 24 (bit 24 of 64-bit field) */
   uint8_t data[8] = { 0, 0, 0, 0x01, 0, 0, 0, 0 };
   bool balance[DALY_MAX_CELLS];
   memset(balance, false, sizeof(balance));
   daly_parse_0x97(data, DALY_MAX_CELLS, balance);
   TEST_ASSERT_TRUE(balance[24]);
   TEST_ASSERT_FALSE(balance[23]);
   TEST_ASSERT_FALSE(balance[25]);
}

/* 0x98: Fault flags — 8 bytes × 8 bits = 64 possible faults mapped via daly_faults[] table. */

void test_parse_0x98_no_faults(void) {
   uint8_t data[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
   char faults[DALY_MAX_FAULTS][64];
   int count = -1;
   daly_parse_0x98(data, faults, &count);
   TEST_ASSERT_EQUAL_INT(0, count);
}

void test_parse_0x98_single_cell_high_l2(void) {
   /* Byte 0 bit 1 → "Cell volt high L2" */
   uint8_t data[8] = { 0x02, 0, 0, 0, 0, 0, 0, 0 };
   char faults[DALY_MAX_FAULTS][64];
   int count = 0;
   daly_parse_0x98(data, faults, &count);
   TEST_ASSERT_EQUAL_INT(1, count);
   TEST_ASSERT_EQUAL_STRING("Cell volt high L2", faults[0]);
}

void test_parse_0x98_multiple_faults(void) {
   /* Byte 0 bit 0 → "Cell volt high L1"
    * Byte 0 bit 2 → "Cell volt low L1"
    * Byte 2 bit 3 → "Dischg OC L2" */
   uint8_t data[8] = { 0x05, 0, 0x08, 0, 0, 0, 0, 0 };
   char faults[DALY_MAX_FAULTS][64];
   int count = 0;
   daly_parse_0x98(data, faults, &count);
   TEST_ASSERT_EQUAL_INT(3, count);
   TEST_ASSERT_EQUAL_STRING("Cell volt high L1", faults[0]);
   TEST_ASSERT_EQUAL_STRING("Cell volt low L1", faults[1]);
   TEST_ASSERT_EQUAL_STRING("Dischg OC L2", faults[2]);
}

/* Checksum */

void test_checksum_zero_bytes(void) {
   uint8_t data[4] = { 0, 0, 0, 0 };
   TEST_ASSERT_EQUAL_HEX8(0x00, daly_checksum(data, sizeof(data)));
}

void test_checksum_known_sum(void) {
   uint8_t data[4] = { 0x01, 0x02, 0x03, 0x04 };
   TEST_ASSERT_EQUAL_HEX8(0x0A, daly_checksum(data, sizeof(data)));
}

void test_checksum_truncates_to_8_bits(void) {
   uint8_t data[3] = { 0xFF, 0xFF, 0x02 };
   /* 0xFF + 0xFF + 0x02 = 0x200, truncated to 0x00 */
   TEST_ASSERT_EQUAL_HEX8(0x00, daly_checksum(data, sizeof(data)));
}

void test_checksum_detects_single_bit_tampering(void) {
   uint8_t good[12] = { 0xA5, 0x40, 0x90, 0x08, 0, 0, 0, 0, 0, 0, 0, 0 };
   uint8_t tampered[12];
   memcpy(tampered, good, sizeof(good));
   uint8_t csum_good = daly_checksum(good, 12);
   tampered[5] ^= 0x01; /* flip one bit in the data area */
   uint8_t csum_bad = daly_checksum(tampered, 12);
   TEST_ASSERT_NOT_EQUAL(csum_good, csum_bad);
}

int main(void) {
   UNITY_BEGIN();

   RUN_TEST(test_parse_0x90_pack_voltage_primary);
   RUN_TEST(test_parse_0x90_pack_voltage_fallback_to_cumulative);
   RUN_TEST(test_parse_0x90_zero_current);
   RUN_TEST(test_parse_0x90_positive_current_charge);
   RUN_TEST(test_parse_0x90_negative_current_discharge);
   RUN_TEST(test_parse_0x90_soc_percentage);

   RUN_TEST(test_parse_0x91_extremes);

   RUN_TEST(test_parse_0x92_temperature_offset);
   RUN_TEST(test_parse_0x92_subzero_temperature);

   RUN_TEST(test_parse_0x93_mos_flags);
   RUN_TEST(test_parse_0x93_both_mosfets_on);

   RUN_TEST(test_parse_0x97_no_cells_balancing);
   RUN_TEST(test_parse_0x97_cell_1_and_8_balancing);
   RUN_TEST(test_parse_0x97_high_byte_cells);

   RUN_TEST(test_parse_0x98_no_faults);
   RUN_TEST(test_parse_0x98_single_cell_high_l2);
   RUN_TEST(test_parse_0x98_multiple_faults);

   RUN_TEST(test_checksum_zero_bytes);
   RUN_TEST(test_checksum_known_sum);
   RUN_TEST(test_checksum_truncates_to_8_bits);
   RUN_TEST(test_checksum_detects_single_bit_tampering);

   return UNITY_END();
}
