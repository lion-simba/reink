/* This file is part of ReInk.
 * Copyright (C) 2008-2016 Alexey Osipov public@alexey.osipov.name
 * Copyright (C) 2014 Andrei Komarovskikh andrei.komarovskikh@gmail.com
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

#include "printers.h"
#include "util.h"

#include <stddef.h>
#include <string.h>

static const struct printer_info printers[] = {
	{
		.name = "Unknown printer",
		.model_name = "Unknown printer",
		.model_code = {0x00, 0x00},
		.twobyte_addresses = 0,
		.inkmap = {
			.mask = 			0,
			.black = 			{0x00, 0x00, 0x00, 0x00},
			.cyan = 			{0x00, 0x00, 0x00, 0x00},
			.magenta = 			{0x00, 0x00, 0x00, 0x00},
			.yellow = 			{0x00, 0x00, 0x00, 0x00},
			.lightcyan = 		{0x00, 0x00, 0x00, 0x00},
			.lightmagenta = 	{0x00, 0x00, 0x00, 0x00},
		},
		.wastemap = {
			.len = 				0,
			.addr = 			{0x00, 0x00, 0x00, 0x00},
		},
	}, {
		.name = "EPSON Stylus Photo 790",
		.model_name = "Stylus Photo 790",
		.model_code = {0x06, 0x31},
		.twobyte_addresses = 0,
		.inkmap = {
			.mask = INK_BLACK | INK_CYAN | INK_MAGENTA | INK_YELLOW | INK_LIGHTCYAN | INK_LIGHTMAGENTA,
			.black = 			{0x02, 0x03, 0x04, 0x05},
			.cyan = 			{0x0e, 0x0f, 0x10, 0x11},
			.magenta = 			{0x0a, 0x0b, 0x0c, 0x0d},
			.yellow = 			{0x06, 0x07, 0x08, 0x09},
			.lightcyan = 		{0x16, 0x17, 0x18, 0x19},
			.lightmagenta = 	{0x12, 0x13, 0x14, 0x15},
		},
		.wastemap = {
			.len = 				2,
			.addr = 			{0x1a, 0x1b, 0x00, 0x00},
		},
	}, {
		.name = "EPSON Stylus Color 580",
		.model_name = "Stylus COLOR 580",
		.model_code = {0x06, 0x1b},
		.twobyte_addresses = 0,
		.inkmap = {
			.mask = INK_BLACK | INK_CYAN | INK_MAGENTA | INK_YELLOW,
			.black = 			{0x44, 0x45, 0x46, 0x47},
			.cyan = 			{0x52, 0x53, 0x54, 0x55},
			.magenta = 			{0x4c, 0x4d, 0x4e, 0x4f},
			.yellow = 			{0x48, 0x49, 0x4a, 0x4b},
			.lightcyan = 		{0x00, 0x00, 0x00, 0x00},
			.lightmagenta = 	{0x00, 0x00, 0x00, 0x00},
		},
		.wastemap = {
			.len = 				2,
			.addr = 			{0x66, 0x67, 0x00, 0x00},
		},
	}, {
		.name = "EPSON Stylus Photo 1290",
		.model_name = "Stylus Photo 1290",
		.model_code = {0x07, 0x19},
		.twobyte_addresses = 0,
		.inkmap = {
			.mask = INK_BLACK | INK_CYAN | INK_MAGENTA | INK_YELLOW | INK_LIGHTCYAN | INK_LIGHTMAGENTA,
			.black = 			{0x44, 0x45, 0x46, 0x47},
			.cyan = 			{0x4c, 0x4d, 0x4e, 0x4f},
			.magenta = 			{0x50, 0x51, 0x52, 0x53},
			.yellow = 			{0x48, 0x49, 0x4a, 0x4b},
			.lightcyan = 		{0x54, 0x55, 0x56, 0x57},
			.lightmagenta = 	{0x58, 0x59, 0x5a, 0x5b},
		},
		.wastemap = {
			.len = 				4,
			.addr = 			{0x6c, 0x6d, 0x6e, 0x6f},
		},
	}, {
		.name = "EPSON Stylus Color 680",
		.model_name = "Stylus COLOR 680",
		.model_code = {0x06, 0x15},
		.twobyte_addresses = 0,
		.inkmap = {
			.mask = INK_BLACK | INK_CYAN | INK_MAGENTA | INK_YELLOW | INK_LIGHTCYAN | INK_LIGHTMAGENTA,
			.black = 			{0x02, 0x03, 0x04, 0x05},
			.cyan = 			{0x0e, 0x0f, 0x10, 0x11},
			.magenta = 			{0x0a, 0x0b, 0x0c, 0x0d},
			.yellow = 			{0x06, 0x07, 0x08, 0x09},
			.lightcyan = 		{0x16, 0x17, 0x18, 0x19},
			.lightmagenta = 	{0x12, 0x13, 0x14, 0x15},
		},
		.wastemap = {
			.len = 				2,
			.addr = 			{0x1a, 0x1b, 0x00, 0x00},
		},
	}, {
		.name = "Epson Stylus Photo T50",
		.model_name = "Epson Stylus Photo T50",
		.model_code = {0x77, 0x00},
		.twobyte_addresses = 0,
		.inkmap = {
			.mask = INK_BLACK | INK_CYAN | INK_MAGENTA | INK_YELLOW | INK_LIGHTCYAN | INK_LIGHTMAGENTA,
			.black = 			{0x02, 0x03, 0x04, 0x05},
			.cyan = 			{0x0e, 0x0f, 0x10, 0x11},
			.magenta = 			{0x0a, 0x0b, 0x0c, 0x0d},
			.yellow = 			{0x06, 0x07, 0x08, 0x09},
			.lightcyan = 		{0x16, 0x17, 0x18, 0x19},
			.lightmagenta = 	{0x12, 0x13, 0x14, 0x15},
		},
		.wastemap = {
			.len = 				4,
			.addr = 			{0x1c, 0x1d, 0x1e, 0x1f},
		},
	}, {
		.name = "Epson Stylus Photo P50",
		.model_name = "Epson Stylus Photo P50",
		.model_code = {0x77, 0x00},
		.twobyte_addresses = 0,
		.inkmap = {
			.mask = INK_BLACK | INK_CYAN | INK_MAGENTA | INK_YELLOW | INK_LIGHTCYAN | INK_LIGHTMAGENTA,
			.black = 			{0x02, 0x03, 0x04, 0x05},
			.cyan = 			{0x0e, 0x0f, 0x10, 0x11},
			.magenta = 			{0x0a, 0x0b, 0x0c, 0x0d},
			.yellow = 			{0x06, 0x07, 0x08, 0x09},
			.lightcyan = 		{0x16, 0x17, 0x18, 0x19},
			.lightmagenta = 	{0x12, 0x13, 0x14, 0x15},
		},
		.wastemap = {
			.len = 				4,
			.addr = 			{0x1c, 0x1d, 0x1e, 0x1f},
		}
	},
};

bool is_unknown_printer(const struct printer_info *printer_info)
{
	return !strcmp(printer_info->model_name, "Unknown printer");
}

const struct printer_info *db_locate_printer_by_model(const char *model)
{
	size_t i;
	const struct printer_info *printer_info;

	for (i = 0; i < ARRAY_SIZE(printers); i++) {
		printer_info = &printers[i];
		if (!strcmp(printer_info->model_name, model))
			return printer_info;
	}

	/* Default to the Unknown printer. */
	return &printers[0];
}
