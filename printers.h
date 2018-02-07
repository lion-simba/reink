/* This file is part of ReInk.
 * Copyright (C) 2008-2016 Alexey Osipov public@alexey.osipov.name
 *
 * ReInk is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * ReInk is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ReInk. If not, see <http://www.gnu.org/licenses/>.
 */
 
#include <stdbool.h>
#include <stdint.h>

//ink counters EEPROM map
#define INK_BLACK			0x01
#define INK_CYAN			0x02
#define INK_MAGENTA			0x04
#define INK_YELLOW			0x08
#define INK_LIGHTCYAN		0x10
#define INK_LIGHTMAGENTA	0x20

typedef struct _ink_map {
	unsigned int mask;
	uint16_t black[4];
	uint16_t cyan[4];
	uint16_t magenta[4];
	uint16_t yellow[4];
	uint16_t lightcyan[4];
	uint16_t lightmagenta[4];
} ink_map_t;

//waste counter EEPROM map
typedef struct _waste_map {
	/* EEPROM locations containing waste ink counters. */
	uint16_t addr[4];
	/* Number of EEPROM locations in 'addr'. */
	uint8_t len;
} waste_map_t;

//the printer
#define MAX_NAME_LEN	100
#define MAX_MODEL_LEN	100
typedef struct printer_info {
	ink_map_t inkmap;
	waste_map_t wastemap;
	/* Human-readable name. */
	unsigned char name[MAX_NAME_LEN];
	/* Printer model name as returned by printer. */
	unsigned char model_name[MAX_MODEL_LEN];
	/* "password" for this printer */
	uint8_t model_code[2];
	/* Does the printer use two-byte EEPROM addresses? */
	bool twobyte_addresses;
} printer_t;

const struct printer_info *db_locate_printer_by_model(const char *model);
bool is_unknown_printer(const struct printer_info *info);
