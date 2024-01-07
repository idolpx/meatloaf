/*
 * psram.h
 *
 *  Created on: May 26, 2013
 *      Author: petera
 */

#ifndef PSRAMFS_H_
#define PSRAMFS_H_
#if defined(__cplusplus)
extern "C" {
#endif

#include "psramfs_config.h"

#define PSRAMFS_OK                       0
#define PSRAMFS_ERR_NOT_MOUNTED          -10000
#define PSRAMFS_ERR_FULL                 -10001
#define PSRAMFS_ERR_NOT_FOUND            -10002
#define PSRAMFS_ERR_END_OF_OBJECT        -10003
#define PSRAMFS_ERR_DELETED              -10004
#define PSRAMFS_ERR_NOT_FINALIZED        -10005
#define PSRAMFS_ERR_NOT_INDEX            -10006
#define PSRAMFS_ERR_OUT_OF_FILE_DESCS    -10007
#define PSRAMFS_ERR_FILE_CLOSED          -10008
#define PSRAMFS_ERR_FILE_DELETED         -10009
#define PSRAMFS_ERR_BAD_DESCRIPTOR       -10010
#define PSRAMFS_ERR_IS_INDEX             -10011
#define PSRAMFS_ERR_IS_FREE              -10012
#define PSRAMFS_ERR_INDEX_SPAN_MISMATCH  -10013
#define PSRAMFS_ERR_DATA_SPAN_MISMATCH   -10014
#define PSRAMFS_ERR_INDEX_REF_FREE       -10015
#define PSRAMFS_ERR_INDEX_REF_LU         -10016
#define PSRAMFS_ERR_INDEX_REF_INVALID    -10017
#define PSRAMFS_ERR_INDEX_FREE           -10018
#define PSRAMFS_ERR_INDEX_LU             -10019
#define PSRAMFS_ERR_INDEX_INVALID        -10020
#define PSRAMFS_ERR_NOT_WRITABLE         -10021
#define PSRAMFS_ERR_NOT_READABLE         -10022
#define PSRAMFS_ERR_CONFLICTING_NAME     -10023
#define PSRAMFS_ERR_NOT_CONFIGURED       -10024

#define PSRAMFS_ERR_NOT_A_FS             -10025
#define PSRAMFS_ERR_MOUNTED              -10026
#define PSRAMFS_ERR_ERASE_FAIL           -10027
#define PSRAMFS_ERR_MAGIC_NOT_POSSIBLE   -10028

#define PSRAMFS_ERR_NO_DELETED_BLOCKS    -10029

#define PSRAMFS_ERR_FILE_EXISTS          -10030

#define PSRAMFS_ERR_NOT_A_FILE           -10031
#define PSRAMFS_ERR_RO_NOT_IMPL          -10032
#define PSRAMFS_ERR_RO_ABORTED_OPERATION -10033
#define PSRAMFS_ERR_PROBE_TOO_FEW_BLOCKS -10034
#define PSRAMFS_ERR_PROBE_NOT_A_FS       -10035
#define PSRAMFS_ERR_NAME_TOO_LONG        -10036

#define PSRAMFS_ERR_IX_MAP_UNMAPPED      -10037
#define PSRAMFS_ERR_IX_MAP_MAPPED        -10038
#define PSRAMFS_ERR_IX_MAP_BAD_RANGE     -10039

#define PSRAMFS_ERR_SEEK_BOUNDS          -10040


#define PSRAMFS_ERR_INTERNAL             -10050

#define PSRAMFS_ERR_TEST                 -10100


// psram file descriptor index type. must be signed
typedef s16_t psramfs_file;
// psram file descriptor flags
typedef u16_t psramfs_flags;
// psram file mode
typedef u16_t psramfs_mode;
// object type
typedef u8_t psramfs_obj_type;

struct psramfs_t;

#if PSRAMFS_HAL_CALLBACK_EXTRA

/* spi read call function type */
typedef s32_t (*psramfs_read)(struct psramfs_t *fs, u32_t addr, u32_t size, u8_t *dst);
/* spi write call function type */
typedef s32_t (*psramfs_write)(struct psramfs_t *fs, u32_t addr, u32_t size, u8_t *src);
/* spi erase call function type */
typedef s32_t (*psramfs_erase)(struct psramfs_t *fs, u32_t addr, u32_t size);

#else // PSRAMFS_HAL_CALLBACK_EXTRA

/* spi read call function type */
typedef s32_t (*psramfs_read)(u32_t addr, u32_t size, u8_t *dst);
/* spi write call function type */
typedef s32_t (*psramfs_write)(u32_t addr, u32_t size, u8_t *src);
/* spi erase call function type */
typedef s32_t (*psramfs_erase)(u32_t addr, u32_t size);
#endif // PSRAMFS_HAL_CALLBACK_EXTRA

/* file system check callback report operation */
typedef enum {
  PSRAMFS_CHECK_LOOKUP = 0,
  PSRAMFS_CHECK_INDEX,
  PSRAMFS_CHECK_PAGE
} psramfs_check_type;

/* file system check callback report type */
typedef enum {
  PSRAMFS_CHECK_PROGRESS = 0,
  PSRAMFS_CHECK_ERROR,
  PSRAMFS_CHECK_FIX_INDEX,
  PSRAMFS_CHECK_FIX_LOOKUP,
  PSRAMFS_CHECK_DELETE_ORPHANED_INDEX,
  PSRAMFS_CHECK_DELETE_PAGE,
  PSRAMFS_CHECK_DELETE_BAD_FILE
} psramfs_check_report;

/* file system check callback function */
#if PSRAMFS_HAL_CALLBACK_EXTRA
typedef void (*psramfs_check_callback)(struct psramfs_t *fs, psramfs_check_type type, psramfs_check_report report,
    u32_t arg1, u32_t arg2);
#else // PSRAMFS_HAL_CALLBACK_EXTRA
typedef void (*psramfs_check_callback)(psramfs_check_type type, psramfs_check_report report,
    u32_t arg1, u32_t arg2);
#endif // PSRAMFS_HAL_CALLBACK_EXTRA

/* file system listener callback operation */
typedef enum {
  /* the file has been created */
  PSRAMFS_CB_CREATED = 0,
  /* the file has been updated or moved to another page */
  PSRAMFS_CB_UPDATED,
  /* the file has been deleted */
  PSRAMFS_CB_DELETED
} psramfs_fileop_type;

/* file system listener callback function */
typedef void (*psramfs_file_callback)(struct psramfs_t *fs, psramfs_fileop_type op, psramfs_obj_id obj_id, psramfs_page_ix pix);

#ifndef PSRAMFS_DBG
#define PSRAMFS_DBG(...) \
    printf(__VA_ARGS__)
#endif
#ifndef PSRAMFS_GC_DBG
#define PSRAMFS_GC_DBG(...) printf(__VA_ARGS__)
#endif
#ifndef PSRAMFS_CACHE_DBG
#define PSRAMFS_CACHE_DBG(...) printf(__VA_ARGS__)
#endif
#ifndef PSRAMFS_CHECK_DBG
#define PSRAMFS_CHECK_DBG(...) printf(__VA_ARGS__)
#endif

/* Any write to the filehandle is appended to end of the file */
#define PSRAMFS_APPEND                   (1<<0)
#define PSRAMFS_O_APPEND                 PSRAMFS_APPEND
/* If the opened file exists, it will be truncated to zero length before opened */
#define PSRAMFS_TRUNC                    (1<<1)
#define PSRAMFS_O_TRUNC                  PSRAMFS_TRUNC
/* If the opened file does not exist, it will be created before opened */
#define PSRAMFS_CREAT                    (1<<2)
#define PSRAMFS_O_CREAT                  PSRAMFS_CREAT
/* The opened file may only be read */
#define PSRAMFS_RDONLY                   (1<<3)
#define PSRAMFS_O_RDONLY                 PSRAMFS_RDONLY
/* The opened file may only be written */
#define PSRAMFS_WRONLY                   (1<<4)
#define PSRAMFS_O_WRONLY                 PSRAMFS_WRONLY
/* The opened file may be both read and written */
#define PSRAMFS_RDWR                     (PSRAMFS_RDONLY | PSRAMFS_WRONLY)
#define PSRAMFS_O_RDWR                   PSRAMFS_RDWR
/* Any writes to the filehandle will never be cached but flushed directly */
#define PSRAMFS_DIRECT                   (1<<5)
#define PSRAMFS_O_DIRECT                 PSRAMFS_DIRECT
/* If PSRAMFS_O_CREAT and PSRAMFS_O_EXCL are set, PSRAMFS_open() shall fail if the file exists */
#define PSRAMFS_EXCL                     (1<<6)
#define PSRAMFS_O_EXCL                   PSRAMFS_EXCL

#define PSRAMFS_SEEK_SET                 (0)
#define PSRAMFS_SEEK_CUR                 (1)
#define PSRAMFS_SEEK_END                 (2)

#define PSRAMFS_TYPE_FILE                (1)
#define PSRAMFS_TYPE_DIR                 (2)
#define PSRAMFS_TYPE_HARD_LINK           (3)
#define PSRAMFS_TYPE_SOFT_LINK           (4)

#ifndef PSRAMFS_LOCK
#define PSRAMFS_LOCK(fs)
#endif

#ifndef PSRAMFS_UNLOCK
#define PSRAMFS_UNLOCK(fs)
#endif

// phys structs

// psram spi configuration struct
typedef struct {
  // physical read function
  psramfs_read hal_read_f;
  // physical write function
  psramfs_write hal_write_f;
  // physical erase function
  psramfs_erase hal_erase_f;
#if PSRAMFS_SINGLETON == 0
  // physical size of the spi flash
  u32_t phys_size;
  // physical offset in spi flash used for psram,
  // must be on block boundary
  u32_t phys_addr;
  // physical size when erasing a block
  u32_t phys_erase_block;

  // logical size of a block, must be on physical
  // block size boundary and must never be less than
  // a physical block
  u32_t log_block_size;
  // logical size of a page, must be at least
  // log_block_size / 8
  u32_t log_page_size;

#endif
#if PSRAMFS_FILEHDL_OFFSET
  // an integer offset added to each file handle
  u16_t fh_ix_offset;
#endif
} psramfs_config;

typedef struct psramfs_t {
  // file system configuration
  psramfs_config cfg;
  // number of logical blocks
  u32_t block_count;

  // cursor for free blocks, block index
  psramfs_block_ix free_cursor_block_ix;
  // cursor for free blocks, entry index
  int free_cursor_obj_lu_entry;
  // cursor when searching, block index
  psramfs_block_ix cursor_block_ix;
  // cursor when searching, entry index
  int cursor_obj_lu_entry;

  // primary work buffer, size of a logical page
  u8_t *lu_work;
  // secondary work buffer, size of a logical page
  u8_t *work;
  // file descriptor memory area
  u8_t *fd_space;
  // available file descriptors
  u32_t fd_count;

  // last error
  s32_t err_code;

  // current number of free blocks
  u32_t free_blocks;
  // current number of busy pages
  u32_t stats_p_allocated;
  // current number of deleted pages
  u32_t stats_p_deleted;
  // flag indicating that garbage collector is cleaning
  u8_t cleaning;
  // max erase count amongst all blocks
  psramfs_obj_id max_erase_count;

#if PSRAMFS_GC_STATS
  u32_t stats_gc_runs;
#endif

#if PSRAMFS_CACHE
  // cache memory
  void *cache;
  // cache size
  u32_t cache_size;
#if PSRAMFS_CACHE_STATS
  u32_t cache_hits;
  u32_t cache_misses;
#endif
#endif

  // check callback function
  psramfs_check_callback check_cb_f;
  // file callback function
  psramfs_file_callback file_cb_f;
  // mounted flag
  u8_t mounted;
  // user data
  void *user_data;
  // config magic
  u32_t config_magic;
} psram;

/* psram file status struct */
typedef struct {
  psramfs_obj_id obj_id;
  u32_t size;
  psramfs_obj_type type;
  psramfs_page_ix pix;
  u8_t name[PSRAMFS_OBJ_NAME_LEN];
#if PSRAMFS_OBJ_META_LEN
  u8_t meta[PSRAMFS_OBJ_META_LEN];
#endif
} psramfs_stat;

struct psramfs_dirent {
  psramfs_obj_id obj_id;
  u8_t name[PSRAMFS_OBJ_NAME_LEN];
  psramfs_obj_type type;
  u32_t size;
  psramfs_page_ix pix;
#if PSRAMFS_OBJ_META_LEN
  u8_t meta[PSRAMFS_OBJ_META_LEN];
#endif
};

typedef struct {
  psram *fs;
  psramfs_block_ix block;
  int entry;
} psramfs_DIR;

#if PSRAMFS_IX_MAP

typedef struct {
  // buffer with looked up data pixes
  psramfs_page_ix *map_buf;
  // precise file byte offset
  u32_t offset;
  // start data span index of lookup buffer
  psramfs_span_ix start_spix;
  // end data span index of lookup buffer
  psramfs_span_ix end_spix;
} psramfs_ix_map;

#endif

// functions

#if PSRAMFS_USE_MAGIC && PSRAMFS_USE_MAGIC_LENGTH && PSRAMFS_SINGLETON==0
/**
 * Special function. This takes a psram config struct and returns the number
 * of blocks this file system was formatted with. This function relies on
 * that following info is set correctly in given config struct:
 *
 * phys_addr, log_page_size, and log_block_size.
 *
 * Also, hal_read_f must be set in the config struct.
 *
 * One must be sure of the correct page size and that the physical address is
 * correct in the probed file system when calling this function. It is not
 * checked if the phys_addr actually points to the start of the file system,
 * so one might get a false positive if entering a phys_addr somewhere in the
 * middle of the file system at block boundary. In addition, it is not checked
 * if the page size is actually correct. If it is not, weird file system sizes
 * will be returned.
 *
 * If this function detects a file system it returns the assumed file system
 * size, which can be used to set the phys_size.
 *
 * Otherwise, it returns an error indicating why it is not regarded as a file
 * system.
 *
 * Note: this function is not protected with PSRAMFS_LOCK and PSRAMFS_UNLOCK
 * macros. It returns the error code directly, instead of as read by
 * PSRAMFS_errno.
 *
 * @param config        essential parts of the physical and logical
 *                      configuration of the file system.
 */
s32_t PSRAMFS_probe_fs(psramfs_config *config);
#endif // PSRAMFS_USE_MAGIC && PSRAMFS_USE_MAGIC_LENGTH && PSRAMFS_SINGLETON==0

/**
 * Initializes the file system dynamic parameters and mounts the filesystem.
 * If PSRAMFS_USE_MAGIC is enabled the mounting may fail with PSRAMFS_ERR_NOT_A_FS
 * if the flash does not contain a recognizable file system.
 * In this case, PSRAMFS_format must be called prior to remounting.
 * @param fs            the file system struct
 * @param config        the physical and logical configuration of the file system
 * @param work          a memory work buffer comprising 2*config->log_page_size
 *                      bytes used throughout all file system operations
 * @param fd_space      memory for file descriptors
 * @param fd_space_size memory size of file descriptors
 * @param cache         memory for cache, may be null
 * @param cache_size    memory size of cache
 * @param check_cb_f    callback function for reporting during consistency checks
 */
s32_t PSRAMFS_mount(psram *fs, psramfs_config *config, u8_t *work,
    u8_t *fd_space, u32_t fd_space_size,
    void *cache, u32_t cache_size,
    psramfs_check_callback check_cb_f);

/**
 * Unmounts the file system. All file handles will be flushed of any
 * cached writes and closed.
 * @param fs            the file system struct
 */
void PSRAMFS_unmount(psram *fs);

/**
 * Creates a new file.
 * @param fs            the file system struct
 * @param path          the path of the new file
 * @param mode          ignored, for posix compliance
 */
s32_t PSRAMFS_creat(psram *fs, const char *path, psramfs_mode mode);

/**
 * Opens/creates a file.
 * @param fs            the file system struct
 * @param path          the path of the new file
 * @param flags         the flags for the open command, can be combinations of
 *                      PSRAMFS_O_APPEND, PSRAMFS_O_TRUNC, PSRAMFS_O_CREAT, PSRAMFS_O_RDONLY,
 *                      PSRAMFS_O_WRONLY, PSRAMFS_O_RDWR, PSRAMFS_O_DIRECT, PSRAMFS_O_EXCL
 * @param mode          ignored, for posix compliance
 */
psramfs_file PSRAMFS_open(psram *fs, const char *path, psramfs_flags flags, psramfs_mode mode);

/**
 * Opens a file by given dir entry.
 * Optimization purposes, when traversing a file system with PSRAMFS_readdir
 * a normal PSRAMFS_open would need to traverse the filesystem again to find
 * the file, whilst PSRAMFS_open_by_dirent already knows where the file resides.
 * @param fs            the file system struct
 * @param e             the dir entry to the file
 * @param flags         the flags for the open command, can be combinations of
 *                      PSRAMFS_APPEND, PSRAMFS_TRUNC, PSRAMFS_CREAT, PSRAMFS_RD_ONLY,
 *                      PSRAMFS_WR_ONLY, PSRAMFS_RDWR, PSRAMFS_DIRECT.
 *                      PSRAMFS_CREAT will have no effect in this case.
 * @param mode          ignored, for posix compliance
 */
psramfs_file PSRAMFS_open_by_dirent(psram *fs, struct psramfs_dirent *e, psramfs_flags flags, psramfs_mode mode);

/**
 * Opens a file by given page index.
 * Optimization purposes, opens a file by directly pointing to the page
 * index in the spi flash.
 * If the page index does not point to a file header PSRAMFS_ERR_NOT_A_FILE
 * is returned.
 * @param fs            the file system struct
 * @param page_ix       the page index
 * @param flags         the flags for the open command, can be combinations of
 *                      PSRAMFS_APPEND, PSRAMFS_TRUNC, PSRAMFS_CREAT, PSRAMFS_RD_ONLY,
 *                      PSRAMFS_WR_ONLY, PSRAMFS_RDWR, PSRAMFS_DIRECT.
 *                      PSRAMFS_CREAT will have no effect in this case.
 * @param mode          ignored, for posix compliance
 */
psramfs_file PSRAMFS_open_by_page(psram *fs, psramfs_page_ix page_ix, psramfs_flags flags, psramfs_mode mode);

/**
 * Reads from given filehandle.
 * @param fs            the file system struct
 * @param fh            the filehandle
 * @param buf           where to put read data
 * @param len           how much to read
 * @returns number of bytes read, or -1 if error
 */
s32_t PSRAMFS_read(psram *fs, psramfs_file fh, void *buf, s32_t len);

/**
 * Writes to given filehandle.
 * @param fs            the file system struct
 * @param fh            the filehandle
 * @param buf           the data to write
 * @param len           how much to write
 * @returns number of bytes written, or -1 if error
 */
s32_t PSRAMFS_write(psram *fs, psramfs_file fh, void *buf, s32_t len);

/**
 * Moves the read/write file offset. Resulting offset is returned or negative if error.
 * lseek(fs, fd, 0, PSRAMFS_SEEK_CUR) will thus return current offset.
 * @param fs            the file system struct
 * @param fh            the filehandle
 * @param offs          how much/where to move the offset
 * @param whence        if PSRAMFS_SEEK_SET, the file offset shall be set to offset bytes
 *                      if PSRAMFS_SEEK_CUR, the file offset shall be set to its current location plus offset
 *                      if PSRAMFS_SEEK_END, the file offset shall be set to the size of the file plus offse, which should be negative
 */
s32_t PSRAMFS_lseek(psram *fs, psramfs_file fh, s32_t offs, int whence);

/**
 * Removes a file by path
 * @param fs            the file system struct
 * @param path          the path of the file to remove
 */
s32_t PSRAMFS_remove(psram *fs, const char *path);

/**
 * Removes a file by filehandle
 * @param fs            the file system struct
 * @param fh            the filehandle of the file to remove
 */
s32_t PSRAMFS_fremove(psram *fs, psramfs_file fh);

/**
 * Truncates a file at given size
 * @param fs            the file system struct
 * @param fh            the filehandle of the file to truncate
 * @param new_size      the new size, must be less than existing file size
 * @returns 0 on success, error code otherwise
 */
s32_t PSRAMFS_ftruncate(psram* fs, psramfs_file fh, u32_t new_size);

/**
 * Gets file status by path
 * @param fs            the file system struct
 * @param path          the path of the file to stat
 * @param s             the stat struct to populate
 */
s32_t PSRAMFS_stat(psram *fs, const char *path, psramfs_stat *s);

/**
 * Gets file status by filehandle
 * @param fs            the file system struct
 * @param fh            the filehandle of the file to stat
 * @param s             the stat struct to populate
 */
s32_t PSRAMFS_fstat(psram *fs, psramfs_file fh, psramfs_stat *s);

/**
 * Flushes all pending write operations from cache for given file
 * @param fs            the file system struct
 * @param fh            the filehandle of the file to flush
 */
s32_t PSRAMFS_fflush(psram *fs, psramfs_file fh);

/**
 * Closes a filehandle. If there are pending write operations, these are finalized before closing.
 * @param fs            the file system struct
 * @param fh            the filehandle of the file to close
 */
s32_t PSRAMFS_close(psram *fs, psramfs_file fh);

/**
 * Renames a file
 * @param fs            the file system struct
 * @param old           path of file to rename
 * @param newPath       new path of file
 */
s32_t PSRAMFS_rename(psram *fs, const char *old, const char *newPath);

#if PSRAMFS_OBJ_META_LEN
/**
 * Updates file's metadata
 * @param fs            the file system struct
 * @param path          path to the file
 * @param meta          new metadata. must be PSRAMFS_OBJ_META_LEN bytes long.
 */
s32_t PSRAMFS_update_meta(psram *fs, const char *name, const void *meta);

/**
 * Updates file's metadata
 * @param fs            the file system struct
 * @param fh            file handle of the file
 * @param meta          new metadata. must be PSRAMFS_OBJ_META_LEN bytes long.
 */
s32_t PSRAMFS_fupdate_meta(psram *fs, psramfs_file fh, const void *meta);
#endif

/**
 * Returns last error of last file operation.
 * @param fs            the file system struct
 */
s32_t PSRAMFS_errno(psram *fs);

/**
 * Clears last error.
 * @param fs            the file system struct
 */
void PSRAMFS_clearerr(psram *fs);

/**
 * Opens a directory stream corresponding to the given name.
 * The stream is positioned at the first entry in the directory.
 * On hydrogen builds the name argument is ignored as hydrogen builds always correspond
 * to a flat file structure - no directories.
 * @param fs            the file system struct
 * @param name          the name of the directory
 * @param d             pointer the directory stream to be populated
 */
psramfs_DIR *PSRAMFS_opendir(psram *fs, const char *name, psramfs_DIR *d);

/**
 * Closes a directory stream
 * @param d             the directory stream to close
 */
s32_t PSRAMFS_closedir(psramfs_DIR *d);

/**
 * Reads a directory into given spifs_dirent struct.
 * @param d             pointer to the directory stream
 * @param e             the dirent struct to be populated
 * @returns null if error or end of stream, else given dirent is returned
 */
struct psramfs_dirent *PSRAMFS_readdir(psramfs_DIR *d, struct psramfs_dirent *e);

/**
 * Runs a consistency check on given filesystem.
 * @param fs            the file system struct
 */
s32_t PSRAMFS_check(psram *fs);

/**
 * Returns number of total bytes available and number of used bytes.
 * This is an estimation, and depends on if there a many files with little
 * data or few files with much data.
 * NB: If used number of bytes exceeds total bytes, a PSRAMFS_check should
 * run. This indicates a power loss in midst of things. In worst case
 * (repeated powerlosses in mending or gc) you might have to delete some files.
 *
 * @param fs            the file system struct
 * @param total         total number of bytes in filesystem
 * @param used          used number of bytes in filesystem
 */
s32_t PSRAMFS_info(psram *fs, u32_t *total, u32_t *used);

/**
 * Formats the entire file system. All data will be lost.
 * The filesystem must not be mounted when calling this.
 *
 * NB: formatting is awkward. Due to backwards compatibility, PSRAMFS_mount
 * MUST be called prior to formatting in order to configure the filesystem.
 * If PSRAMFS_mount succeeds, PSRAMFS_unmount must be called before calling
 * PSRAMFS_format.
 * If PSRAMFS_mount fails, PSRAMFS_format can be called directly without calling
 * PSRAMFS_unmount first.
 *
 * @param fs            the file system struct
 */
s32_t PSRAMFS_format(psram *fs);

/**
 * Returns nonzero if psram is mounted, or zero if unmounted.
 * @param fs            the file system struct
 */
u8_t PSRAMFS_mounted(psram *fs);

/**
 * Tries to find a block where most or all pages are deleted, and erase that
 * block if found. Does not care for wear levelling. Will not move pages
 * around.
 * If parameter max_free_pages are set to 0, only blocks with only deleted
 * pages will be selected.
 *
 * NB: the garbage collector is automatically called when psram needs free
 * pages. The reason for this function is to give possibility to do background
 * tidying when user knows the system is idle.
 *
 * Use with care.
 *
 * Setting max_free_pages to anything larger than zero will eventually wear
 * flash more as a block containing free pages can be erased.
 *
 * Will set err_no to PSRAMFS_OK if a block was found and erased,
 * PSRAMFS_ERR_NO_DELETED_BLOCK if no matching block was found,
 * or other error.
 *
 * @param fs             the file system struct
 * @param max_free_pages maximum number allowed free pages in block
 */
s32_t PSRAMFS_gc_quick(psram *fs, u16_t max_free_pages);

/**
 * Will try to make room for given amount of bytes in the filesystem by moving
 * pages and erasing blocks.
 * If it is physically impossible, err_no will be set to PSRAMFS_ERR_FULL. If
 * there already is this amount (or more) of free space, PSRAMFS_gc will
 * silently return. It is recommended to call PSRAMFS_info before invoking
 * this method in order to determine what amount of bytes to give.
 *
 * NB: the garbage collector is automatically called when psram needs free
 * pages. The reason for this function is to give possibility to do background
 * tidying when user knows the system is idle.
 *
 * Use with care.
 *
 * @param fs            the file system struct
 * @param size          amount of bytes that should be freed
 */
s32_t PSRAMFS_gc(psram *fs, u32_t size);

/**
 * Check if EOF reached.
 * @param fs            the file system struct
 * @param fh            the filehandle of the file to check
 */
s32_t PSRAMFS_eof(psram *fs, psramfs_file fh);

/**
 * Get position in file.
 * @param fs            the file system struct
 * @param fh            the filehandle of the file to check
 */
s32_t PSRAMFS_tell(psram *fs, psramfs_file fh);

/**
 * Registers a callback function that keeps track on operations on file
 * headers. Do note, that this callback is called from within internal psram
 * mechanisms. Any operations on the actual file system being callbacked from
 * in this callback will mess things up for sure - do not do this.
 * This can be used to track where files are and move around during garbage
 * collection, which in turn can be used to build location tables in ram.
 * Used in conjuction with PSRAMFS_open_by_page this may improve performance
 * when opening a lot of files.
 * Must be invoked after mount.
 *
 * @param fs            the file system struct
 * @param cb_func       the callback on file operations
 */
s32_t PSRAMFS_set_file_callback_func(psram *fs, psramfs_file_callback cb_func);

#if PSRAMFS_IX_MAP

/**
 * Maps the first level index lookup to a given memory map.
 * This will make reading big files faster, as the memory map will be used for
 * looking up data pages instead of searching for the indices on the physical
 * medium. When mapping, all affected indicies are found and the information is
 * copied to the array.
 * Whole file or only parts of it may be mapped. The index map will cover file
 * contents from argument offset until and including arguments (offset+len).
 * It is valid to map a longer range than the current file size. The map will
 * then be populated when the file grows.
 * On garbage collections and file data page movements, the map array will be
 * automatically updated. Do not tamper with the map array, as this contains
 * the references to the data pages. Modifying it from outside will corrupt any
 * future readings using this file descriptor.
 * The map will no longer be used when the file descriptor closed or the file
 * is unmapped.
 * This can be useful to get faster and more deterministic timing when reading
 * large files, or when seeking and reading a lot within a file.
 * @param fs      the file system struct
 * @param fh      the file handle of the file to map
 * @param map     a psramfs_ix_map struct, describing the index map
 * @param offset  absolute file offset where to start the index map
 * @param len     length of the mapping in actual file bytes
 * @param map_buf the array buffer for the look up data - number of required
 *                elements in the array can be derived from function
 *                PSRAMFS_bytes_to_ix_map_entries given the length
 */
s32_t PSRAMFS_ix_map(psram *fs, psramfs_file fh, psramfs_ix_map *map,
    u32_t offset, u32_t len, psramfs_page_ix *map_buf);

/**
 * Unmaps the index lookup from this filehandle. All future readings will
 * proceed as normal, requiring reading of the first level indices from
 * physical media.
 * The map and map buffer given in function PSRAMFS_ix_map will no longer be
 * referenced by psram.
 * It is not strictly necessary to unmap a file before closing it, as closing
 * a file will automatically unmap it.
 * @param fs      the file system struct
 * @param fh      the file handle of the file to unmap
 */
s32_t PSRAMFS_ix_unmap(psram *fs, psramfs_file fh);

/**
 * Moves the offset for the index map given in function PSRAMFS_ix_map. Parts or
 * all of the map buffer will repopulated.
 * @param fs      the file system struct
 * @param fh      the mapped file handle of the file to remap
 * @param offset  new absolute file offset where to start the index map
 */
s32_t PSRAMFS_ix_remap(psram *fs, psramfs_file fh, u32_t offs);

/**
 * Utility function to get number of psramfs_page_ix entries a map buffer must
 * contain on order to map given amount of file data in bytes.
 * See function PSRAMFS_ix_map and PSRAMFS_ix_map_entries_to_bytes.
 * @param fs      the file system struct
 * @param bytes   number of file data bytes to map
 * @return        needed number of elements in a psramfs_page_ix array needed to
 *                map given amount of bytes in a file
 */
s32_t PSRAMFS_bytes_to_ix_map_entries(psram *fs, u32_t bytes);

/**
 * Utility function to amount of file data bytes that can be mapped when
 * mapping a file with buffer having given number of psramfs_page_ix entries.
 * See function PSRAMFS_ix_map and PSRAMFS_bytes_to_ix_map_entries.
 * @param fs      the file system struct
 * @param map_page_ix_entries   number of entries in a psramfs_page_ix array
 * @return        amount of file data in bytes that can be mapped given a map
 *                buffer having given amount of psramfs_page_ix entries
 */
s32_t PSRAMFS_ix_map_entries_to_bytes(psram *fs, u32_t map_page_ix_entries);

#endif // PSRAMFS_IX_MAP


#if PSRAMFS_TEST_VISUALISATION
/**
 * Prints out a visualization of the filesystem.
 * @param fs            the file system struct
 */
s32_t PSRAMFS_vis(psram *fs);
#endif

#if PSRAMFS_BUFFER_HELP
/**
 * Returns number of bytes needed for the filedescriptor buffer given
 * amount of file descriptors.
 */
u32_t PSRAMFS_buffer_bytes_for_filedescs(psram *fs, u32_t num_descs);

#if PSRAMFS_CACHE
/**
 * Returns number of bytes needed for the cache buffer given
 * amount of cache pages.
 */
u32_t PSRAMFS_buffer_bytes_for_cache(psram *fs, u32_t num_pages);
#endif
#endif

#if PSRAMFS_CACHE
#endif
#if defined(__cplusplus)
}
#endif

#endif /* PSRAMFS_H_ */
