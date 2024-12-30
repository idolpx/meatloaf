/*
 * cbmfile.c - CBM file handling.
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

/* #define DEBUGCBMFILE */

#include <stdio.h>
#include <string.h>

#include "archdep.h"
#include "cbmdos.h"
#include "charset.h"
#include "fileio.h"
#include "lib.h"
#include "rawfile.h"
#include "types.h"

#include "cbmfile.h"

#ifdef DEBUGCBMFILE
#define DBG(x)  printf x
#else
#define DBG(x)
#endif


fileio_info_t *cbmfile_open(const char *file_name, const char *path,
                            unsigned int command, unsigned int type)
{
    uint8_t *cbm_name;
    fileio_info_t *info;
    struct rawfile_info_s *rawfile;
    char *fsname;

    fsname = lib_strdup(file_name);

    if (!(command & FILEIO_COMMAND_FSNAME)) {
        charset_petconvstring((uint8_t *)fsname, CONVERT_TO_ASCII);
    }

    rawfile = rawfile_open(fsname, path, command & FILEIO_COMMAND_MASK);

    lib_free(fsname);

    if (rawfile == NULL) {
        return NULL;
    }

    cbm_name = (uint8_t *)lib_strdup(file_name);

    if (command & FILEIO_COMMAND_FSNAME) {
        charset_petconvstring(cbm_name, CONVERT_TO_PETSCII);
    }

    info = lib_malloc(sizeof(fileio_info_t));
    info->name = cbm_name;
    info->length = (unsigned int)strlen((char *)cbm_name);
    info->type = type;
    info->format = FILEIO_FORMAT_RAW;
    info->rawfile = rawfile;

    return info;
}

void cbmfile_close(fileio_info_t *info)
{
    rawfile_destroy(info->rawfile);
}

unsigned int cbmfile_read(fileio_info_t *info, uint8_t *buf, unsigned int len)
{
    return rawfile_read(info->rawfile, buf, len);
}

unsigned int cbmfile_write(fileio_info_t *info, uint8_t *buf, unsigned int len)
{
    return rawfile_write(info->rawfile, buf, len);
}

unsigned int cbmfile_ferror(fileio_info_t *info)
{
    return rawfile_ferror(info->rawfile);
}

unsigned int cbmfile_rename(const char *src_name, const char *dst_name,
                            const char *path)
{
    char *src_cbm, *dst_cbm;
    unsigned int rc;

    DBG(("cbmfile_rename '%s' to '%s'\n", src_name, dst_name));

    src_cbm = lib_strdup(src_name);
    dst_cbm = lib_strdup(dst_name);

    charset_petconvstring((uint8_t *)src_cbm, CONVERT_TO_ASCII);
    charset_petconvstring((uint8_t *)dst_cbm, CONVERT_TO_ASCII);

    rc = rawfile_rename(src_cbm, dst_cbm, path);

    lib_free(src_cbm);
    lib_free(dst_cbm);

    return rc;
}

unsigned int cbmfile_scratch(const char *file_name, const char *path)
{
    char *src_cbm;
    unsigned int rc;

    src_cbm = lib_strdup(file_name);
    charset_petconvstring((uint8_t *)src_cbm, CONVERT_TO_ASCII);

    rc = rawfile_remove(src_cbm, path);

    lib_free(src_cbm);

    return rc;
}

unsigned int cbmfile_get_bytes_left(struct fileio_info_s *info)
{
    return rawfile_get_bytes_left(info->rawfile);
}

unsigned int cbmfile_seek(struct fileio_info_s *info, off_t offset, int whence)
{
    return rawfile_seek(info->rawfile, offset, whence);
}

unsigned int cbmfile_tell(struct fileio_info_s *info)
{
    return rawfile_tell(info->rawfile);
}
