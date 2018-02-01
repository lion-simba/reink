/*
 * Small utilities that are useful across the board
 *
 * This file is part of ReInk.
 * Copyright (C) 2018 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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

#include "util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

void hexdump(const void *memory, size_t length)
{
	int i;
	uint8_t *line;
	int all_zero = 0;
	int all_one = 0;
	size_t num_bytes;

	for (i = 0; i < length; i += 16) {
		int j;
		num_bytes = (length - i) < 16 ? (length - i) : 16;
		line = ((uint8_t *)memory) + i;

		all_zero++;
		all_one++;
		for (j = 0; j < num_bytes; j++) {
			if (line[j] != 0) {
				all_zero = 0;
				break;
			}
		}

		for (j = 0; j < num_bytes; j++) {
			if (line[j] != 0xff) {
				all_one = 0;
				break;
			}
		}

		if ((all_zero < 2) && (all_one < 2)) {
			reink_log("%.04x:", i);
			for (j = 0; j < num_bytes; j++)
				reink_log(" %02x", line[j]);
			for (; j < 16; j++)
				reink_log("   ");
			reink_log("  ");
			for (j = 0; j < num_bytes; j++)
				reink_log("%c",
					  isprint(line[j]) ? line[j] : '.');
				reink_log("\n");
		} else if ((all_zero == 2) || (all_one == 2)) {
			reink_log("...\n");
		}
	}
}

int reink_do_log(const char *fmt, va_list args)
{
	return vfprintf(stderr, fmt, args);
}

int reink_log(const char *fmt, ...)
{
	int ret;
	va_list args;

	va_start(args, fmt);
	ret = reink_do_log(fmt, args);
	va_end(args);

	return ret;
}
