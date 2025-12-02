/*
 * log.c - Logging facility.
 *
 * Written by
 *  Ettore Perazzoli <ettore@comm2000.it>
 *  groepaz <groepaz@gmx.net>
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

/* #define DBGLOGGING */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archdep.h"
#include "lib.h"
#include "log.h"
#include "util.h"

#define MAX_LOGS 10
static const char *logs[MAX_LOGS] = {NULL};


log_t log_open(const char *id)
{
  for(log_t i=0; i<MAX_LOGS; i++)
    if( logs[i]==NULL )
      {
        logs[i]=id;
        return i;
      }

  return LOG_DEFAULT;
}


int log_close(log_t log)
{
  if(log>=0 && log<MAX_LOGS)
    logs[log] = NULL;
  return 0;
}

void log_close_all(void)
{
  for(log_t i=0; i<MAX_LOGS; i++)
    logs[i]=NULL;
}

/******************************************************************************/

/* helper function for formatted output to default logger (stdout) */
static int log_archdep(const char *pretxt, const char *logtxt)
{
    /*
     * ------ Split into single lines ------
     */
    int rc = 0;

    const char *beg = logtxt;
    const char *end = logtxt + strlen(logtxt) + 1;

    while (beg < end) {
        char *eol = strchr(beg, '\n');

        if (eol) {
            *eol = '\0';
        }

        /* output to stdout */
        if (archdep_default_logger(*beg ? pretxt : "", beg) < 0) {
            rc = -1;
            break;
        }

        if (!eol) {
            break;
        }

        beg = eol + 1;
    }

    return rc;
}

/* main log helper */
static int log_helper(log_t log, unsigned int level, const char *format,
                      va_list ap)
{
    static const char * const level_strings[8] = {
        "",             /* LOG_LEVEL_NONE */
        "Fatal - ",     /* LOG_LEVEL_FATAL */
        "Error - ",     /* LOG_LEVEL_ERROR */
        "Warning - ",   /* LOG_LEVEL_WARNING */
        "",             /* LOG_LEVEL_INFO */
        "",             /* LOG_LEVEL_VERBOSE */
        "",             /* LOG_LEVEL_DEBUG */
        ""              /* LOG_LEVEL_ALL */
    };

    const char *lvlstr = level_strings[(level >> 5) & 7];

    signed int logi = (signed int)log;
    int rc = 0;
    char *pretxt = NULL;
    char *logtxt = NULL;
    char *terminalpre = NULL;
    char *terminaltxt = NULL;

    if (logi != LOG_DEFAULT) {
      if ((logi < 0) || (logi >= MAX_LOGS) || (logs[logi] == NULL)) {
        logi = LOG_DEFAULT;
      }
    }

    /* prepend the log_t prefix, and the loglevel string */
    if ((logi == LOG_DEFAULT) || (*logs[logi] == '\0')) {
        pretxt = lib_msprintf("%s", lvlstr);
    } else {
        pretxt = lib_msprintf("%s: %s", logs[logi], lvlstr);
    }
    /* build the log string */
    logtxt = lib_mvsprintf(format, ap);

    if( log_archdep(pretxt, logtxt) < 0 )
      rc = -1;

    lib_free(pretxt);
    lib_free(logtxt);
    return rc;
}

/******************************************************************************
 High level log functions
 ******************************************************************************/

int log_out(log_t log, unsigned int level, const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(log, level, format, ap);
    va_end(ap);
    return rc;
}

int log_message(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(log, LOG_LEVEL_INFO, format, ap);
    va_end(ap);
    return rc;
}

int log_warning(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(log, LOG_LEVEL_WARNING, format, ap);
    va_end(ap);
    return rc;
}

int log_error(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(log, LOG_LEVEL_ERROR, format, ap);
    va_end(ap);
    return rc;
}

int log_fatal(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(log, LOG_LEVEL_FATAL, format, ap);
    va_end(ap);
    return rc;
}

int log_verbose(log_t log, const char *format, ...)
{
    va_list ap;
    int rc = 0;

    va_start(ap, format);
    rc = log_helper(log, LOG_LEVEL_VERBOSE, format, ap);
    va_end(ap);
    return rc;
}

int log_debug(log_t log, const char *format, ...)
{
    va_list ap;
    int rc = 0;

    va_start(ap, format);
    rc = log_helper(log, LOG_LEVEL_DEBUG, format, ap);
    va_end(ap);
    return rc;
}

int log_printf_vdrive(const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = log_helper(LOG_DEFAULT, LOG_LEVEL_DEBUG, format, ap);
    va_end(ap);
    return rc;
}
