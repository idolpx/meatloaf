/** \file   zfile.c
 * \brief   Transparent handling of compressed files
 * \author  Ettore Perazzoli <ettore@comm2000.it>
 * \author  Andreas Boose <viceteam@t-online.de>
 * \author  Bas Wassink <b.wassink@ziggo.nl>
 *
 * ARCHIVE, ZIPCODE and LYNX supports added by
 *  Teemu Rantanen <tvr@cs.hut.fi>
 */

/*
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

/* This code might be improved a lot...  */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include "archdep.h"
#include "lib.h"
#include "log.h"
#include "util.h"

#include "zfile.h"


/* ------------------------------------------------------------------------- */

//#define DEBUG_ZFILE

#ifdef DEBUG_ZFILE
#define ZDEBUG(a) log_printf_vdrive  a
#else
#define ZDEBUG(a)
#endif

/* We could add more here...  */
enum compression_type {
    COMPR_NONE,
    COMPR_ZIP
};

/* This defines a linked list of all the compressed files that have been
   opened.  */
struct zfile_s {
    char *tmp_name;              /* Name of the temporary file.  */
    char *orig_name;             /* Name of the original file.  */
    int write_mode;              /* Non-zero if the file is open for writing.*/
    ADFILE *stream;               /* Associated stdio-style stream.  */
    ADFILE *fd;                   /* Associated file descriptor.  */
    enum compression_type type;  /* Compression algorithm.  */
    struct zfile_s *prev, *next; /* Link to the previous and next nodes.  */
    zfile_action_t action;       /* action on close */
    char *request_string;        /* ui string for action=ZFILE_REQUEST */
};
typedef struct zfile_s zfile_t;

static zfile_t *zfile_list = NULL;

static log_t zlog = LOG_DEFAULT;

/* ------------------------------------------------------------------------- */

static int zinit_done = 0;


static void zfile_list_destroy(void)
{
    zfile_t *p;

    for (p = zfile_list; p != NULL; ) {
        zfile_t *next;

        lib_free(p->orig_name);
        lib_free(p->tmp_name);
        next = p->next;
        lib_free(p);
        p = next;
    }

    zfile_list = NULL;
}

static int zinit(void)
{
    zlog = log_open("ZFile");

    /* Free the `zfile_list' if not empty.  */
    zfile_list_destroy();

    zinit_done = 1;

    return 0;
}

/* Add one zfile to the list.  `orig_name' is automatically expanded to the
   complete path.  */
static void zfile_list_add(const char *tmp_name,
                           const char *orig_name,
                           enum compression_type type,
                           int write_mode,
                           ADFILE *stream, ADFILE *fd)
{
    zfile_t *new_zfile = lib_malloc(sizeof(zfile_t));

    /* Make sure we have the complete path of the file.  */
    archdep_expand_path(&new_zfile->orig_name, orig_name);

    /* The new zfile becomes first on the list.  */
    new_zfile->tmp_name = tmp_name ? lib_strdup(tmp_name) : NULL;
    new_zfile->write_mode = write_mode;
    new_zfile->stream = stream;
    new_zfile->fd = fd;
    new_zfile->type = type;
    new_zfile->action = ZFILE_KEEP;
    new_zfile->request_string = NULL;
    new_zfile->next = zfile_list;
    new_zfile->prev = NULL;
    if (zfile_list != NULL) {
        zfile_list->prev = new_zfile;
    }
    zfile_list = new_zfile;
}

void zfile_shutdown(void)
{
    zfile_list_destroy();
}

/* ------------------------------------------------------------------------ */

/* Uncompression.  */

#ifdef ARCHDEP_NO_MINIZ

static char *try_uncompress_with_miniz(const char *zipfilename)
{
  return NULL;
}

#else

#include "miniz.h"
static char *try_uncompress_with_miniz(const char *zipfilename)
{
  char *res = NULL;
  mz_zip_archive zip_archive;

  size_t l = strlen(zipfilename);
  if(l <= 3 || util_strcasecmp(zipfilename + l - 3, "zip") != 0) 
    return NULL;

  ZDEBUG(("try_uncompress_with_miniz: %s", zipfilename));

  memset(&zip_archive, 0, sizeof(zip_archive));

  if( mz_zip_reader_init_file(&zip_archive, zipfilename, 0) )
    {
      // extract the first file in the archive
      int idx = 0;

      mz_uint ns = mz_zip_reader_get_filename(&zip_archive, idx, NULL, 0);
      if( ns>0 )
        {
          res = lib_malloc(ns);
          if( mz_zip_reader_get_filename(&zip_archive, idx, res, ns)!=ns )
            {
              ZDEBUG(("try_uncompress_with_miniz: unable to get entry file name"));
              lib_free(res);
              res = NULL;
            }
        }

      if( res==NULL || archdep_file_exists(res) )
        {
          if( res!=NULL ) lib_free(res);
          res = archdep_tmpnam();
        }

      ZDEBUG(("try_uncompress_with_miniz: extracting file %s", res));
      if( !mz_zip_reader_extract_to_file(&zip_archive, idx, res, 0) )
        {
          ZDEBUG(("try_uncompress_with_miniz: failed to extract file"));
          lib_free(res);
          res = NULL;
        }
    }
  else
    { ZDEBUG(("try_uncompress_with_miniz: failed to initialize zipfile")); }
  
  mz_zip_reader_end(&zip_archive);
  return res;
}

#endif


/* Try to uncompress file `name' using the algorithms we know of.  If this is
   not possible, return `COMPR_NONE'.  Otherwise, uncompress the file into a
   temporary file, return the type of algorithm used and the name of the
   temporary file in `tmp_name'.  If `write_mode' is non-zero and the
   returned `tmp_name' has zero length, then the file cannot be accessed in
   write mode.  */
static enum compression_type try_uncompress(const char *name,
                                            char **tmp_name,
                                            int write_mode)
{
    if ((*tmp_name = try_uncompress_with_miniz(name)) != NULL) {
        return COMPR_ZIP;
    }

  return COMPR_NONE;
}

/* ------------------------------------------------------------------------- */

/* Compression.  */


/* Compress `src' into `dest' using algorithm `type'.  */
static int zfile_compress(const char *src, const char *dest,
                          enum compression_type type)
{
  return 0;
}

/* ------------------------------------------------------------------------ */

/* Here we have the actual fopen and fclose wrappers.

   These functions work exactly like the standard library versions, but
   handle compression and decompression automatically.  When a file is
   opened, we check whether it looks like a compressed file of some kind.
   If so, we uncompress it and then actually open the uncompressed version.
   When a file that was opened for writing is closed, we re-compress the
   uncompressed version and update the original file.  */

/* `fopen()' wrapper.  */
ADFILE *zfile_fopen(const char *name, const char *mode)
{
    char *tmp_name;
    ADFILE *stream;
    enum compression_type type;
    int write_mode = 0;

    if (!zinit_done) {
        zinit();
    }

    if (name == NULL || name[0] == 0) {
      return archdep_fnofile();
    }

    /* Do we want to write to this file?  */
    if ((strchr(mode, 'w') != NULL) || (strchr(mode, '+') != NULL)) {
        write_mode = 1;
    }

    /* Check for write permissions.  */
    if (write_mode && archdep_access(name, ARCHDEP_ACCESS_W_OK) < 0) {
        return archdep_fnofile();
    }

    type = try_uncompress(name, &tmp_name, write_mode);
    if (type == COMPR_NONE) {
        stream = archdep_fopen(name, mode);
        if ( !archdep_fisopen(stream) ) {
            return archdep_fnofile();
        }
        zfile_list_add(NULL, name, type, write_mode, stream, archdep_fnofile());
        return stream;
    } else if (*tmp_name == '\0') {
        errno = EACCES;
        return archdep_fnofile();
    }

    /* Open the uncompressed version of the file.  */
    stream = archdep_fopen(tmp_name, mode);
    if( !archdep_fisopen(stream) ) {
        return archdep_fnofile();
    }

    zfile_list_add(tmp_name, name, type, write_mode, stream, archdep_fnofile());

    /* now we don't need the archdep_tmpnam allocation any more */
    lib_free(tmp_name);

    return stream;
}

/* Handle close-action of a file.  `ptr' points to the zfile to close.  */
static int handle_close_action(zfile_t *ptr)
{
    if (ptr == NULL || ptr->orig_name == NULL) {
        return -1;
    }

    switch (ptr->action) {
        case ZFILE_KEEP:
            break;
        case ZFILE_REQUEST:
        /*
          ui_zfile_close_request(ptr->orig_name, ptr->request_string);
          break;
        */
        case ZFILE_DEL:
            if (archdep_remove(ptr->orig_name) < 0) {
                log_error(zlog, "Cannot unlink `%s': %s",
                          ptr->orig_name, strerror(errno));
            }
            break;
    }
    return 0;
}

/* Handle close of a (compressed file). `ptr' points to the zfile to close.  */
static int handle_close(zfile_t *ptr)
{
    ZDEBUG(("handle_close: closing `%s' (`%s'), write_mode = %d",
            ptr->tmp_name ? ptr->tmp_name : "(null)",
            ptr->orig_name, ptr->write_mode));

    if (ptr->tmp_name) {
        /* Recompress into the original file.  */
        if (ptr->orig_name
            && ptr->write_mode
            && zfile_compress(ptr->tmp_name, ptr->orig_name, ptr->type)) {
            return -1;
        }

        /* Remove temporary file.  */
        if (archdep_remove(ptr->tmp_name) < 0) {
            log_error(zlog, "Cannot unlink `%s': %s", ptr->tmp_name, strerror(errno));
        }
    }

    handle_close_action(ptr);

    /* Remove item from list.  */
    if (ptr->prev != NULL) {
        ptr->prev->next = ptr->next;
    } else {
        zfile_list = ptr->next;
    }

    if (ptr->next != NULL) {
        ptr->next->prev = ptr->prev;
    }

    if (ptr->orig_name) {
        lib_free(ptr->orig_name);
    }
    if (ptr->tmp_name) {
        lib_free(ptr->tmp_name);
    }
    if (ptr->request_string) {
        lib_free(ptr->request_string);
    }

    lib_free(ptr);

    return 0;
}

/* `fclose()' wrapper.  */
int zfile_fclose(ADFILE *stream)
{
    zfile_t *ptr;

    if (!zinit_done) {
        errno = EBADF;
        return -1;
    }

    /* Search for the matching file in the list.  */
    for (ptr = zfile_list; ptr != NULL; ptr = ptr->next) {
      if ( archdep_fissame(ptr->stream, stream) ) {
            /* Close temporary file.  */
            if (archdep_fclose(stream) == -1) {
                return -1;
            }
            if (handle_close(ptr) < 0) {
                errno = EBADF;
                return -1;
            }

            return 0;
        }
    }

    return archdep_fclose(stream);
}

int zfile_close_action(const char *filename, zfile_action_t action,
                       const char *request_str)
{
    char *fullname = NULL;
    zfile_t *p = zfile_list;

    archdep_expand_path(&fullname, filename);

    while (p != NULL) {
        if (p->orig_name && !strcmp(p->orig_name, fullname)) {
            p->action = action;
            p->request_string = request_str ? lib_strdup(request_str) : NULL;
            lib_free(fullname);
            return 0;
        }
        p = p->next;
    }

    lib_free(fullname);
    return -1;
}
