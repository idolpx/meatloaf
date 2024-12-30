/*
 * lib.c - Library function wrappers, mostly for memory alloc/free tracking.
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "types.h"
#include "log.h"
#include "lib.h"


/*----------------------------------------------------------------------------*/


void *lib_malloc(size_t size)
{
  return malloc(size);
}


void *lib_calloc(size_t nmemb, size_t size)
{
  return calloc(nmemb, size);
}


void *lib_realloc(void *ptr, size_t size)
{
  return realloc(ptr, size);
}


void lib_free(void *ptr)
{
    free(ptr);
}


char *lib_strdup(const char *str)
{
  return strdup(str);
}


char *lib_mvsprintf(const char *fmt, va_list ap)
{
    int maxlen;
    char *p;
    va_list args;

    va_copy(args, ap);
    /* Get the length of the formatted string, NOT containing
       the terminating zero. */
    maxlen = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    /* Test if this size is valid */
    if (maxlen < 0) {
        return NULL;
    }

    /* Alloc required size */
    if ((p = lib_malloc(maxlen + 1)) == NULL) {
        return NULL;
    }

    /* At this point, this call should never fail */
    vsnprintf(p, maxlen + 1, fmt, ap);

    return p;
}

char *lib_msprintf(const char *fmt, ...)
{
    va_list args;
    char *buf;

    va_start(args, fmt);
    buf = lib_mvsprintf(fmt, args);
    va_end(args);

    return buf;
}

/** \brief Return a copy of str with leading and trailing whitespace removed */
char *lib_strdup_trimmed(char *str)
{
    char *copy;
    char *trimmed;
    size_t len;

    copy = lib_strdup(str);
    trimmed = copy;

    /* trim leading whitespace */
    while (*trimmed != '\0') {
        if (*trimmed == ' ' ||
            *trimmed == '\t' ||
            *trimmed == '\n' ||
            *trimmed == '\r') {
            trimmed++;
        } else {
            break;
        }
    }

    /* trim trailing whitespace */
    while ((len = strlen(trimmed)) > 0) {
        if (trimmed[len - 1] == ' ' ||
            trimmed[len - 1] == '\t' ||
            trimmed[len - 1] == '\n' ||
            trimmed[len - 1] == '\r') {
            trimmed[len - 1] = '\0';
        } else {
            break;
        }
    }

    trimmed = lib_strdup(trimmed);

    lib_free(copy);

    return trimmed;
}
