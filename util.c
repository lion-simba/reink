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

#include <stdio.h>
#include <stdarg.h>

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
