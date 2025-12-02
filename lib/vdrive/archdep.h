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

#ifndef VICE_ARCHDEP_H
#define VICE_ARCHDEP_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#if defined(ARDUINO)
typedef void ADFILE;
#elif defined(ESP_PLATFORM)
// Meatloaf (handles compression on its own)
#define ARCHDEP_NO_MINIZ
typedef void ADFILE;
#elif defined(__GNUC__)
typedef FILE ADFILE;
#else
#error "unsupported platform"
#endif


/* Modes for fopen().  */
#define MODE_READ              "rb"
#define MODE_READ_TEXT         "rt"
#define MODE_READ_WRITE        "rb+"
#define MODE_WRITE             "wb"
#define MODE_WRITE_TEXT        "wt"
#define MODE_APPEND            "ab"
#define MODE_APPEND_READ_WRITE "ab+"

#define ARCHDEP_DIR_SEP_STR "\\"
#define ARCHDEP_DIR_SEP_CHR '\\'

#define ARCHDEP_R_OK   4
#define ARCHDEP_W_OK   2
#define ARCHDEP_X_OK   1
#define ARCHDEP_F_OK   0

#define PRI_SIZE_T     "Iu"
#define PRI_SSIZE_T    "Id"


#define ARCHDEP_ACCESS_R_OK 4 // file readable
#define ARCHDEP_ACCESS_W_OK 2 // file writable
#define ARCHDEP_ACCESS_X_OK 1 // file executable
#define ARCHDEP_ACCESS_F_OK 0 // file exists

#define ARCHDEP_OPENDIR_NO_HIDDEN_FILES 1
#define ARCHDEP_OPENDIR_ALL_FILES       0


#ifdef __cplusplus
extern "C"
{
#endif

/** \brief  Directory object
 *
 * Contains a list of directories and a list of files for a given host directory.
 *
 * For the position in the directory the API concatenates the dirs and files
 * with the dirs coming first. The directories and files are sorted in ascending
 * order in a case-sensitive manner and thus sorted Unix-style and not Windows-
 * style (where case folding is normally applied).
 */
typedef struct archdep_dir_s {
    char **dirs;        /**< list of directories */
    char **files;       /**< list of files */
    int dir_amount;     /**< number of entries in `dirs` */
    int file_amount;    /**< number of entries in `files` */
    int pos;            /**< position in directory, adding together dirs and
                             files, with dirs coming first */
    int dirs_size;      /**< size of the `dirs` array, in number of elements */
    int files_size;     /**< size of the `files` array, in number of elements */
} archdep_dir_t;


typedef struct archdep_tm_s {
  int tm_wday;
  int tm_year;
  int tm_mon;
  int tm_mday;
  int tm_hour;
  int tm_min;
  int tm_sec;
} archdep_tm_t;


int archdep_default_logger(const char *level_string, const char *txt);
int archdep_default_logger_is_terminal(void);
int archdep_expand_path(char **return_path, const char *orig_name);
archdep_tm_t *archdep_get_time(archdep_tm_t *ts);
void archdep_exit(int excode);

// --- file functions
ADFILE *archdep_fnofile();
ADFILE *archdep_fopen(const char* filename, const char* mode);
int    archdep_fisopen(ADFILE *file);
int    archdep_ferror(ADFILE *file);
int    archdep_fclose(ADFILE *file);
size_t archdep_fread(void* buffer, size_t size, size_t count, ADFILE *stream);
size_t archdep_fwrite(const void* buffer, size_t size, size_t count, ADFILE *stream);
int    archdep_fflush(ADFILE *file);
long int archdep_ftell(ADFILE *stream);
int    archdep_fseek(ADFILE *stream, long int offset, int whence);
void   archdep_frewind(ADFILE *file);
int    archdep_fissame(ADFILE *file1, ADFILE *file2);
off_t  archdep_file_size(ADFILE *stream);
char  *archdep_tmpnam(void);

bool archdep_file_exists(const char *path);
int  archdep_remove(const char *path);
int  archdep_rename(const char *oldpath, const char *newpath);
int  archdep_stat(const char *filename, size_t *len, unsigned int *isdir);
int  archdep_access(const char *pathname, int mode);

// --- directory functions
archdep_dir_t *archdep_opendir(const char *path, int mode);
const char *archdep_readdir(archdep_dir_t *dir);
void archdep_closedir(archdep_dir_t *dir);

uint32_t archdep_get_available_heap();

#ifdef __cplusplus
}
#endif

#endif
