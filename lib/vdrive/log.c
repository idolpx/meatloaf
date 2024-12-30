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

#ifdef DBGLOGGING
#define DBG(x) printf x
#else
#define DBG(x)
#endif

#ifdef USE_VICE_THREAD
/*
 * It was observed that stdout logging from the UI thread under Windows
 * wasn't reliable, possibly only when the vice mainlock has not been
 * obtained.
 *
 * This lock serialises access to logging functions without requiring
 * ownership of the main lock.
 *
 *******************************************************************
 * ANY NEW NON-STATIC FUNCTIONS NEED CALLS TO LOCK() and UNLOCK(). *
 *******************************************************************
 */

#include <pthread.h>
static pthread_mutex_t log_lock;

#define LOCK() { log_init_locks(); pthread_mutex_lock(&log_lock); }
#define UNLOCK() { pthread_mutex_unlock(&log_lock); }
#define UNLOCK_AND_RETURN_INT(i) { int result = (i); UNLOCK(); return result; }

#else /* #ifdef USE_VICE_THREAD */

#define LOCK()
#define UNLOCK()
#define UNLOCK_AND_RETURN_INT(i) return (i)

#endif /* #ifdef USE_VICE_THREAD */

static int log_locks_initialized = 0;
static void log_init_locks(void);

static char **logs = NULL;
static log_t num_logs = 0;

static int log_limit_early = -1; /* -1 means not set */

/* resources */

static int log_limit = LOG_LIMIT_DEBUG; /* before the default is set, we want all messages */

static int log_to_stdout = 1;
static int log_colorize = 1;

/* ------------------------------------------------------------------------- */

int log_set_limit(int n)
{
    LOCK();

    if ((n < 0) || (n > 0xff)) {
        UNLOCK_AND_RETURN_INT(-1);
    }

    log_limit = n;
    UNLOCK_AND_RETURN_INT(0);
}

/* called by code that is executed *before* the resources are registered */
int log_set_limit_early(int n)
{
    LOCK();

    log_limit = n;
    log_limit_early = n;

    UNLOCK_AND_RETURN_INT(0);
}

static void log_init_locks(void)
{
    if (log_locks_initialized == 0) {
#ifdef USE_VICE_THREAD
        pthread_mutexattr_t lock_attributes;
        pthread_mutexattr_init(&lock_attributes);
        pthread_mutexattr_settype(&lock_attributes, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&log_lock, &lock_attributes);
#endif
        log_locks_initialized = 1;
    }
    return;
}

/* called via main_program()->archdep_init() */
int log_early_init(int argc, char **argv)
{
    int i;

    log_init_locks();

    DBG(("log_early_init: %d %s\n", argc, argv[0]));
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            DBG(("log_early_init: %d %s\n", i, argv[i]));
            if ((strcmp("-verbose", argv[i]) == 0) || (strcmp("--verbose", argv[i]) == 0)) {
                log_set_limit_early(LOG_LIMIT_VERBOSE);
                break;
            } else if ((strcmp("-silent", argv[i]) == 0) || (strcmp("--silent", argv[i]) == 0)) {
                log_set_limit_early(LOG_LIMIT_SILENT);
                break;
            } else if ((strcmp("-debug", argv[i]) == 0) || (strcmp("--debug", argv[i]) == 0)) {
                log_set_limit_early(LOG_LIMIT_DEBUG);
                break;
            }
        }
    }
    return 0;
}

/******************************************************************************/

/******************************************************************************/

log_t log_open(const char *id)
{
    log_t new_log = 0;
    log_t i;

    LOCK();
    for (i = 0; i < num_logs; i++) {
        if (logs[i] == NULL) {
            new_log = i;
            break;
        }
    }
    if (i == num_logs) {
        new_log = num_logs++;
        logs = lib_realloc(logs, sizeof(*logs) * num_logs);
    }

    logs[new_log] = lib_strdup(id);

    UNLOCK_AND_RETURN_INT(new_log);
}

int log_close(log_t log)
{
    LOCK();

    /*printf("log_close(%s) = %d\n", logs[(unsigned int)log], (int)log);*/
    if (logs[(unsigned int)log] == NULL) {
        UNLOCK_AND_RETURN_INT(-1);
    }

    lib_free(logs[(unsigned int)log]);
    logs[(unsigned int)log] = NULL;

    UNLOCK_AND_RETURN_INT(0);
}

void log_close_all(void)
{
    log_t i;

    LOCK();

    for (i = 0; i < num_logs; i++) {
        log_close(i);
    }

    lib_free(logs);
    logs = NULL;

    UNLOCK();
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
        LOG_COL_LRED "Fatal" LOG_COL_OFF " - ",     /* LOG_LEVEL_FATAL */
        LOG_COL_LRED "Error" LOG_COL_OFF " - ",     /* LOG_LEVEL_ERROR */
        LOG_COL_LMAGENTA "Warning" LOG_COL_OFF " - ",   /* LOG_LEVEL_WARNING */
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
    char *nocolorpre = NULL;
    char *nocolortxt = NULL;
    char *terminalpre = NULL;
    char *terminaltxt = NULL;

    /* exit early if there is no log enabled */
    if ((log_limit < level) ||
        ((log_to_stdout == 0) )) {
        return 0;
    }

    if (logi != LOG_DEFAULT) {
        if ((logs == NULL) || (logi < 0) || (logi >= num_logs) || (logs[logi] == NULL)) {
            DBG(("log_helper: internal error (invalid id or closed log), message follows:\n"));
            logi = LOG_DEFAULT;
        }
    }

    /* prepend the log_t prefix, and the loglevel string */
    if ((logi == LOG_DEFAULT) || (*logs[logi] == '\0')) {
        pretxt = lib_msprintf("%s", lvlstr);
    } else {
        pretxt = lib_msprintf(LOG_COL_LWHITE "%s" LOG_COL_OFF ": %s", logs[logi], lvlstr);
    }
    /* build the log string */
    logtxt = lib_mvsprintf(format, ap);

    if (log_colorize) {
        terminalpre = pretxt;
        terminaltxt = logtxt;
    } else {
        terminalpre = nocolorpre;
        terminaltxt = nocolortxt;
    }

    if (log_to_stdout) {
        /* FIXME: we should force colors off here, if the standard logger goes
                  into a file (because stdout was redirected) */
        if (archdep_default_logger_is_terminal() == 0) {
            terminalpre = nocolorpre;
            terminaltxt = nocolortxt;
        }
        /* output to stdout */
        if (log_archdep(terminalpre, terminaltxt) < 0) {
            rc = -1;
        }
    }

    lib_free(pretxt);
    lib_free(logtxt);
    lib_free(nocolorpre);
    lib_free(nocolortxt);
    return rc;
}

/******************************************************************************
 High level log functions
 ******************************************************************************/

int log_out(log_t log, unsigned int level, const char *format, ...)
{
    va_list ap;
    int rc;

    LOCK();

    va_start(ap, format);
    rc = log_helper(log, level, format, ap);
    va_end(ap);

    UNLOCK_AND_RETURN_INT(rc);
}

int log_message(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    LOCK();

    va_start(ap, format);
    rc = log_helper(log, LOG_LEVEL_INFO, format, ap);
    va_end(ap);

    UNLOCK_AND_RETURN_INT(rc);
}

int log_warning(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    LOCK();

    va_start(ap, format);
    rc = log_helper(log, LOG_LEVEL_WARNING, format, ap);
    va_end(ap);

    UNLOCK_AND_RETURN_INT(rc);
}

int log_error(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    LOCK();

    va_start(ap, format);
    rc = log_helper(log, LOG_LEVEL_ERROR, format, ap);
    va_end(ap);

    UNLOCK_AND_RETURN_INT(rc);
}

int log_fatal(log_t log, const char *format, ...)
{
    va_list ap;
    int rc;

    LOCK();

    va_start(ap, format);
    rc = log_helper(log, LOG_LEVEL_FATAL, format, ap);
    va_end(ap);

    UNLOCK_AND_RETURN_INT(rc);
}

int log_verbose(log_t log, const char *format, ...)
{
    va_list ap;
    int rc = 0;

    LOCK();

    va_start(ap, format);
    rc = log_helper(log, LOG_LEVEL_VERBOSE, format, ap);
    va_end(ap);

    UNLOCK_AND_RETURN_INT(rc);
}

int log_debug(log_t log, const char *format, ...)
{
    va_list ap;
    int rc = 0;

    LOCK();

    va_start(ap, format);
    rc = log_helper(log, LOG_LEVEL_DEBUG, format, ap);
    va_end(ap);

    UNLOCK_AND_RETURN_INT(rc);
}

int log_printf_vdrive(const char *format, ...)
{
    va_list ap;
    int rc;

    LOCK();

    va_start(ap, format);
    rc = log_helper(LOG_DEFAULT, LOG_LEVEL_DEBUG, format, ap);
    va_end(ap);

    UNLOCK_AND_RETURN_INT(rc);
}
