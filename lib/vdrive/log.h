/*
 * log.h - Logging facility.
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

#ifndef VICE_LOG_H
#define VICE_LOG_H

#include <stdio.h>

/* values passed into the log helper (log_out->log_helper) */
#define LOG_LEVEL_NONE      0x00
#define LOG_LEVEL_FATAL     0x20
#define LOG_LEVEL_ERROR     0x40
#define LOG_LEVEL_WARNING   0x60
#define LOG_LEVEL_INFO      0x80
#define LOG_LEVEL_VERBOSE   0xa0
#define LOG_LEVEL_DEBUG     0xc0
#define LOG_LEVEL_ALL       0xff

/* values used to set the log level (log_set_limit, log_set_limit_early) */

/* errors only */
#define LOG_LIMIT_SILENT    (LOG_LEVEL_WARNING - 1)
/* all messages, except verbose+debug */
#define LOG_LIMIT_STANDARD  (LOG_LEVEL_VERBOSE - 1)
/* all messages, except debug */
#define LOG_LIMIT_VERBOSE   (LOG_LEVEL_DEBUG - 1)
/* all messages */
#define LOG_LIMIT_DEBUG     (LOG_LEVEL_ALL)

/* for individual log streams */
typedef signed int log_t;
#define LOG_DEFAULT ((log_t)-1)

log_t log_open(const char *id);
int log_close(log_t log);
void log_close_all(void);

/* actual log functions */

int log_out(log_t log, unsigned int level, const char *format, ...);

int log_debug(log_t log, const char *format, ...);
int log_verbose(log_t log, const char *format, ...);
int log_message(log_t log, const char *format, ...);
int log_warning(log_t log, const char *format, ...);
int log_error(log_t log, const char *format, ...);
int log_fatal(log_t log, const char *format, ...);

/* simple way to print to the default log, at debug level */
int log_printf_vdrive(const char *format, ...);

#endif
