/*
 * rawfile.c - Raw file handling.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
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

/* #define DEBUGRAWFILE */

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

#include "archdep.h"
#include "fileio.h"
#include "lib.h"
#include "log.h"
#include "util.h"

#include "rawfile.h"

#ifdef DEBUGRAWFILE
#define DBG(x)  log_printf_vdrive x
#else
#define DBG(x)
#endif

struct rawfile_info_s {
    ADFILE *fd;
    char *name;
    char *path;
    unsigned int read_only;
};
typedef struct rawfile_info_s rawfile_info_t;


rawfile_info_t *rawfile_open(const char *file_name, const char *path,
                             unsigned int command)
{
    rawfile_info_t *info;
    char *complete;
    ADFILE *fd;
    const char *mode = NULL;
    unsigned int isdir;
    size_t len;

    if (path == NULL) {
        complete = lib_strdup(file_name);
    } else {
        complete = util_concat(path, ARCHDEP_DIR_SEP_STR, file_name, NULL);
    }

    switch (command) {
        case FILEIO_COMMAND_STAT:
        case FILEIO_COMMAND_READ:
            mode = MODE_READ;
            break;
        case FILEIO_COMMAND_READ_WRITE:
            mode = MODE_READ_WRITE;
            break;
        case FILEIO_COMMAND_WRITE:
        case FILEIO_COMMAND_OVERWRITE:
            mode = MODE_WRITE;
            break;
        case FILEIO_COMMAND_APPEND:
            mode = MODE_APPEND;
            break;
        case FILEIO_COMMAND_APPEND_READ:
            mode = MODE_APPEND_READ_WRITE;
            break;
        default:
            return NULL;
    }

    if (archdep_stat(complete, &len, &isdir) != 0) {
        /* if stat failed exit early, except in write mode
           (since opening a non existing file creates a new file) */
        if (command != FILEIO_COMMAND_WRITE &&
            command != FILEIO_COMMAND_OVERWRITE) {
            lib_free(complete);
            return NULL;
        }
    } else {
        if (command == FILEIO_COMMAND_WRITE) {
        /* A real drive doesn't overwrite an existing file,
           so we should not either.
            Use FILEIO_COMMAND_OVERWRITE to go ahead and
            overwrite the file anyway. */
            lib_free(complete);
            return NULL;
        }
    }

    info = lib_malloc(sizeof(rawfile_info_t));
    if ((isdir) && (command == FILEIO_COMMAND_STAT)) {
      info->fd = archdep_fnofile();
        info->read_only = 1;
    } else {
        fd = archdep_fopen(complete, mode);
        if( !archdep_fisopen(fd) ) {
            lib_free(complete);
            lib_free(info);
            return NULL;
        }
        info->fd = fd;
        info->read_only = 0;
    }

    util_fname_split(complete, &(info->path), &(info->name));

    lib_free(complete);

    return info;
}

void rawfile_destroy(rawfile_info_t *info)
{
    if (info != NULL) {
      if ( archdep_fisopen(info->fd) ) {
            archdep_fclose(info->fd);
        }
        lib_free(info->name);
        lib_free(info->path);
        lib_free(info);
    }
}

unsigned int rawfile_read(rawfile_info_t *info, uint8_t *buf, unsigned int len)
{
  if ( archdep_fisopen(info->fd) ) {
        return (unsigned int)archdep_fread(buf, 1, len, info->fd);
    }
    return -1;
}

unsigned int rawfile_write(rawfile_info_t *info, uint8_t *buf, unsigned int len)
{
  if ( archdep_fisopen(info->fd) ) {
        return (unsigned int)archdep_fwrite(buf, 1, len, info->fd);
    }
    return -1;
}

unsigned int rawfile_get_bytes_left(struct rawfile_info_s *info)
{
    /* this is fucked */
    long old_pos = archdep_ftell(info->fd);
    size_t size;
    archdep_fseek(info->fd, 0, SEEK_END);
    size = archdep_ftell(info->fd);
    archdep_fseek(info->fd, old_pos, SEEK_SET);
    return (unsigned int)(size - old_pos);
}

unsigned int rawfile_seek(struct rawfile_info_s *info, off_t offset, int whence)
{
    return archdep_fseek(info->fd, offset, whence);
}

unsigned int rawfile_tell(struct rawfile_info_s *info)
{
    return (unsigned int)archdep_ftell(info->fd);
}

unsigned int rawfile_ferror(rawfile_info_t *info)
{
    return (unsigned int)archdep_ferror(info->fd);
}

unsigned int rawfile_rename(const char *src_name, const char *dst_name,
                            const char *path)
{
    char *complete_src, *complete_dst;
    int rc;

    DBG(("rawfile_rename '%s' to '%s'", src_name, dst_name));

    if (path == NULL) {
        complete_src = lib_strdup(src_name);
        complete_dst = lib_strdup(dst_name);
    } else {
        complete_src = util_concat(path, ARCHDEP_DIR_SEP_STR, src_name, NULL);
        complete_dst = util_concat(path, ARCHDEP_DIR_SEP_STR, dst_name, NULL);
    }

    /* if dest name exists, produce "file exists" error */
    if (archdep_file_exists(complete_dst)) {
        lib_free(complete_src);
        lib_free(complete_dst);
        return FILEIO_FILE_EXISTS;
    }

    rc = archdep_rename(complete_src, complete_dst);
    DBG(("rawfile_rename rename returned: %d errno: %d", rc, errno));
    lib_free(complete_src);
    lib_free(complete_dst);

    if (rc < 0) {
        if (errno == EPERM) {
            return FILEIO_FILE_PERMISSION;
        }
        return FILEIO_FILE_NOT_FOUND;
    }

    return FILEIO_FILE_OK;
}

unsigned int rawfile_remove(const char *src_name, const char *path)
{
    char *complete_src;
    int rc;

    if (path == NULL) {
        complete_src = lib_strdup(src_name);
    } else {
        complete_src = util_concat(path, ARCHDEP_DIR_SEP_STR, src_name, NULL);
    }

    rc = archdep_remove(complete_src);

    lib_free(complete_src);

    if (rc < 0) {
        return FILEIO_FILE_NOT_FOUND;
    }

    return FILEIO_FILE_SCRATCHED;
}
