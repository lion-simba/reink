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

#ifndef UTIL_H
#define UTIL_H

#include <stdarg.h>
#include <string.h>

void hexdump(const void *memory, size_t length);
int reink_do_log(const char *fmt, va_list args);
int reink_log(const char *fmt, ...);

#endif /* UTIL_H */
