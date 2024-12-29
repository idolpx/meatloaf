/*
 * lib.h - Library function wrappers, mostly for memory alloc/free tracking.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *  Marco van den Heuvel <blackystardust68@yahoo.com>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#ifndef VICE_LIB_H
#define VICE_LIB_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "types.h"
#include "log.h"


char *lib_msprintf(const char *fmt, ...);
char *lib_mvsprintf(const char *fmt, va_list args);

void *lib_malloc(size_t size);
void *lib_calloc(size_t nmemb, size_t size);
void *lib_realloc(void *p, size_t size);
void lib_free(void *ptr);

char *lib_strdup(const char *str);
char *lib_strdup_trimmed(char *str);

#endif
