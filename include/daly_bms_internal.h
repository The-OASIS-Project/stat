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
 * Daly BMS internal helpers exposed for unit testing. Not part of the public
 * API — only daly_bms.c and test files should include this header.
 */

#ifndef DALY_BMS_INTERNAL_H
#define DALY_BMS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "daly_bms.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t daly_checksum(const uint8_t *data, size_t len);
uint16_t daly_get_u16be(const uint8_t *data, int offset);

void daly_parse_0x90(const uint8_t *data, daly_pack_summary_t *pack);
void daly_parse_0x91(const uint8_t *data, daly_extremes_t *extremes);
void daly_parse_0x92(const uint8_t *data, daly_temps_t *temps);
void daly_parse_0x93(const uint8_t *data, daly_mos_caps_t *mos);
void daly_parse_0x97(const uint8_t *data, int cell_count, bool *balance);
void daly_parse_0x98(const uint8_t *data, char faults[][64], int *fault_count);

#ifdef __cplusplus
}
#endif

#endif /* DALY_BMS_INTERNAL_H */
