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

#if 0
#include "archdep.h"

static void lib_log_heap(const char *msg, uint32_t mem1, uint32_t s, uint32_t mem2, const char *p)
{
  static char buf[100];
  snprintf(buf, 100, "%s: %u -- %u --> %u : %p", msg, mem1, s, mem2, p);
  buf[99]=0;
  archdep_default_logger("", buf);
}

void *lib_malloc(size_t size)
{
  uint32_t mem1 = archdep_get_available_heap();
  void *res = malloc(size);
  uint32_t mem2 = archdep_get_available_heap();
  lib_log_heap("lib_malloc ", mem1, size, mem2, res);
  return res;
}


void *lib_calloc(size_t nmemb, size_t size)
{
  uint32_t mem1 = archdep_get_available_heap();
  void *res = calloc(nmemb, size);
  uint32_t mem2 = archdep_get_available_heap();
  lib_log_heap("lib_calloc ", mem1, nmemb*size, mem2, res);
  return res;
}


void *lib_realloc(void *ptr, size_t size)
{
  uint32_t mem1 = archdep_get_available_heap();
  void *res = realloc(ptr, size);
  uint32_t mem2 = archdep_get_available_heap();
  lib_log_heap("lib_realloc", mem1, size, mem2, res);
  return res;
}


void lib_free(void *ptr)
{
  if( ptr!=NULL )
    {
      uint32_t mem1 = archdep_get_available_heap();
      free(ptr);
      uint32_t mem2 = archdep_get_available_heap();
      lib_log_heap("lib_free   ", mem1, 0, mem2, ptr);
    }
}


char *lib_strdup(const char *str)
{
  uint32_t mem1 = archdep_get_available_heap();
  char *res = strdup(str);
  uint32_t mem2 = archdep_get_available_heap();
  lib_log_heap("lib_strdup ", mem1, strlen(str)+1, mem2, res);
  return res;
}

#else

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

#endif

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
