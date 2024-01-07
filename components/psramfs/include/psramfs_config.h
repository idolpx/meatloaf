/*
 * SPDX-FileCopyrightText: 2013-2017 Peter Andersson (pelleplutt1976<at>gmail.com)
 *
 * SPDX-License-Identifier: MIT
 */
/*
 * psramfs_config.h
 *
 *  Created on: Jul 3, 2013
 *      Author: petera
 */

#ifndef PSRAMFS_CONFIG_H_
#define PSRAMFS_CONFIG_H_

// ----------- 8< ------------
// Following includes are for the linux test build of psram
// These may/should/must be removed/altered/replaced in your target
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <sdkconfig.h>
#include <esp_log.h>
#include <assert.h>
#include "esp_assert.h"

// compile time switches
#define PSRAMFS_TAG "PSRAMFS"

// Set generic psram debug output call.
#if CONFIG_PSRAMFS_DBG
#define PSRAMFS_DBG(...)             ESP_LOGD(PSRAMFS_TAG, __VA_ARGS__)
#else
#define PSRAMFS_DBG(...)
#endif
#if CONFIG_PSRAMFS_API_DBG
#define PSRAMFS_API_DBG(...)         ESP_LOGD(PSRAMFS_TAG, __VA_ARGS__)
#else
#define PSRAMFS_API_DBG(...)
#endif
#if CONFIG_PSRAMFS_DBG
#define PSRAMFS_GC_DBG(...)          ESP_LOGD(PSRAMFS_TAG, __VA_ARGS__)
#else
#define PSRAMFS_GC_DBG(...)
#endif
#if CONFIG_PSRAMFS_CACHE_DBG
#define PSRAMFS_CACHE_DBG(...)       ESP_LOGD(PSRAMFS_TAG, __VA_ARGS__)
#else
#define PSRAMFS_CACHE_DBG(...)
#endif
#if CONFIG_PSRAMFS_CHECK_DBG
#define PSRAMFS_CHECK_DBG(...)       ESP_LOGD(PSRAMFS_TAG, __VA_ARGS__)
#else
#define PSRAMFS_CHECK_DBG(...)
#endif

// needed types
typedef int32_t s32_t;
typedef uint32_t u32_t;
typedef int16_t s16_t;
typedef uint16_t u16_t;
typedef int8_t s8_t;
typedef uint8_t u8_t;

struct psramfs_t;
extern void psramfs_api_lock(struct psramfs_t *fs);
extern void psramfs_api_unlock(struct psramfs_t *fs);

// Defines psram debug print formatters
// some general signed number
#define _SPIPRIi   "%d"
// address
#define _SPIPRIad  "%08x"
// block
#define _SPIPRIbl  "%04x"
// page
#define _SPIPRIpg  "%04x"
// span index
#define _SPIPRIsp  "%04x"
// file descriptor
#define _SPIPRIfd  "%d"
// file object id
#define _SPIPRIid  "%04x"
// file flags
#define _SPIPRIfl  "%02x"


// Enable/disable API functions to determine exact number of bytes
// for filedescriptor and cache buffers. Once decided for a configuration,
// this can be disabled to reduce flash.
#define PSRAMFS_BUFFER_HELP              0

// Enables/disable memory read caching of nucleus file system operations.
// If enabled, memory area must be provided for cache in PSRAMFS_mount.
#ifdef CONFIG_PSRAMFS_CACHE
#define PSRAMFS_CACHE                (1)
#else
#define PSRAMFS_CACHE                (0)
#endif
#if PSRAMFS_CACHE
// Enables memory write caching for file descriptors in hydrogen
#ifdef CONFIG_PSRAMFS_CACHE_WR
#define PSRAMFS_CACHE_WR             (1)
#else
#define PSRAMFS_CACHE_WR             (0)
#endif

// Enable/disable statistics on caching. Debug/test purpose only.
#ifdef CONFIG_PSRAMFS_CACHE_STATS
#define PSRAMFS_CACHE_STATS          (1)
#else
#define PSRAMFS_CACHE_STATS          (0)
#endif
#endif

// Always check header of each accessed page to ensure consistent state.
// If enabled it will increase number of reads, will increase flash.
#ifdef CONFIG_PSRAMFS_PAGE_CHECK
#define PSRAMFS_PAGE_CHECK           (1)
#else
#define PSRAMFS_PAGE_CHECK           (0)
#endif

// Define maximum number of gc runs to perform to reach desired free pages.
#define PSRAMFS_GC_MAX_RUNS              CONFIG_PSRAMFS_GC_MAX_RUNS

// Enable/disable statistics on gc. Debug/test purpose only.
#ifdef CONFIG_PSRAMFS_GC_STATS
#define PSRAMFS_GC_STATS             (1)
#else
#define PSRAMFS_GC_STATS             (0)
#endif

// Garbage collecting examines all pages in a block which and sums up
// to a block score. Deleted pages normally gives positive score and
// used pages normally gives a negative score (as these must be moved).
// To have a fair wear-leveling, the erase age is also included in score,
// whose factor normally is the most positive.
// The larger the score, the more likely it is that the block will
// picked for garbage collection.

// Garbage collecting heuristics - weight used for deleted pages.
#define PSRAMFS_GC_HEUR_W_DELET          (5)
// Garbage collecting heuristics - weight used for used pages.
#define PSRAMFS_GC_HEUR_W_USED           (-1)
// Garbage collecting heuristics - weight used for time between
// last erased and erase of this block.
#define PSRAMFS_GC_HEUR_W_ERASE_AGE      (50)

// Object name maximum length. Note that this length include the
// zero-termination character, meaning maximum string of characters
// can at most be PSRAMFS_OBJ_NAME_LEN - 1.
#define PSRAMFS_OBJ_NAME_LEN             (CONFIG_PSRAMFS_OBJ_NAME_LEN)

// Maximum length of the metadata associated with an object.
// Setting to non-zero value enables metadata-related API but also
// changes the on-disk format, so the change is not backward-compatible.
//
// Do note: the meta length must never exceed
// logical_page_size - (PSRAMFS_OBJ_NAME_LEN + PSRAMFS_PAGE_EXTRA_SIZE)
//
// This is derived from following:
// logical_page_size - (PSRAMFS_OBJ_NAME_LEN + sizeof(psramfs_page_header) +
// psramfs_object_ix_header fields + at least some LUT entries)
#define PSRAMFS_OBJ_META_LEN             (CONFIG_PSRAMFS_META_LENGTH)
#define PSRAMFS_PAGE_EXTRA_SIZE          (64)
ESP_STATIC_ASSERT(PSRAMFS_OBJ_META_LEN + PSRAMFS_OBJ_NAME_LEN + PSRAMFS_PAGE_EXTRA_SIZE
        <= CONFIG_PSRAMFS_PAGE_SIZE, "PSRAMFS_OBJ_META_LEN or PSRAMFS_OBJ_NAME_LEN too long");

// Size of buffer allocated on stack used when copying data.
// Lower value generates more read/writes. No meaning having it bigger
// than logical page size.
#define PSRAMFS_COPY_BUFFER_STACK        (256)

// Enable this to have an identifiable psram filesystem. This will look for
// a magic in all sectors to determine if this is a valid psram system or
// not on mount point. If not, PSRAMFS_format must be called prior to mounting
// again.
#ifdef CONFIG_PSRAMFS_USE_MAGIC
#define PSRAMFS_USE_MAGIC                (1)
#else
#define PSRAMFS_USE_MAGIC                (0)
#endif

#if PSRAMFS_USE_MAGIC
// Only valid when PSRAMFS_USE_MAGIC is enabled. If PSRAMFS_USE_MAGIC_LENGTH is
// enabled, the magic will also be dependent on the length of the filesystem.
// For example, a filesystem configured and formatted for 4 megabytes will not
// be accepted for mounting with a configuration defining the filesystem as 2
// megabytes.
#ifdef CONFIG_PSRAMFS_USE_MAGIC_LENGTH
#define PSRAMFS_USE_MAGIC_LENGTH         (1)
#else
#define PSRAMFS_USE_MAGIC_LENGTH         (0)
#endif
#endif

// PSRAMFS_LOCK and PSRAMFS_UNLOCK protects psram from reentrancy on api level
// These should be defined on a multithreaded system

// define this to enter a mutex if you're running on a multithreaded system
#define PSRAMFS_LOCK(fs)   psramfs_api_lock(fs)
// define this to exit a mutex if you're running on a multithreaded system
#define PSRAMFS_UNLOCK(fs) psramfs_api_unlock(fs)

// Enable if only one psram instance with constant configuration will exist
// on the target. This will reduce calculations, flash and memory accesses.
// Parts of configuration must be defined below instead of at time of mount.
#define PSRAMFS_SINGLETON 0

// Enable this if your target needs aligned data for index tables
#define PSRAMFS_ALIGNED_OBJECT_INDEX_TABLES      0

// Enable this if you want the HAL callbacks to be called with the psram struct
#define PSRAMFS_HAL_CALLBACK_EXTRA               1

// Enable this if you want to add an integer offset to all file handles
// (psramfs_file). This is useful if running multiple instances of psram on
// same target, in order to recognise to what psram instance a file handle
// belongs.
// NB: This adds config field fh_ix_offset in the configuration struct when
// mounting, which must be defined.
#define PSRAMFS_FILEHDL_OFFSET                   0

// Enable this to compile a read only version of psram.
// This will reduce binary size of psram. All code comprising modification
// of the file system will not be compiled. Some config will be ignored.
// HAL functions for erasing and writing to spi-flash may be null. Cache
// can be disabled for even further binary size reduction (and ram savings).
// Functions modifying the fs will return PSRAMFS_ERR_RO_NOT_IMPL.
// If the file system cannot be mounted due to aborted erase operation and
// PSRAMFS_USE_MAGIC is enabled, PSRAMFS_ERR_RO_ABORTED_OPERATION will be
// returned.
// Might be useful for e.g. bootloaders and such.
#define PSRAMFS_READ_ONLY                        0

// Enable this to add a temporal file cache using the fd buffer.
// The effects of the cache is that PSRAMFS_open will find the file faster in
// certain cases. It will make it a lot easier for psram to find files
// opened frequently, reducing number of readings from the spi flash for
// finding those files.
// This will grow each fd by 6 bytes. If your files are opened in patterns
// with a degree of temporal locality, the system is optimized.
// Examples can be letting psram serve web content, where one file is the css.
// The css is accessed for each html file that is opened, meaning it is
// accessed almost every second time a file is opened. Another example could be
// a log file that is often opened, written, and closed.
// The size of the cache is number of given file descriptors, as it piggybacks
// on the fd update mechanism. The cache lives in the closed file descriptors.
// When closed, the fd know the whereabouts of the file. Instead of forgetting
// this, the temporal cache will keep handling updates to that file even if the
// fd is closed. If the file is opened again, the location of the file is found
// directly. If all available descriptors become opened, all cache memory is
// lost.
#define PSRAMFS_TEMPORAL_FD_CACHE                1

// Temporal file cache hit score. Each time a file is opened, all cached files
// will lose one point. If the opened file is found in cache, that entry will
// gain PSRAMFS_TEMPORAL_CACHE_HIT_SCORE points. One can experiment with this
// value for the specific access patterns of the application. However, it must
// be between 1 (no gain for hitting a cached entry often) and 255.
#define PSRAMFS_TEMPORAL_CACHE_HIT_SCORE         4

// Enable to be able to map object indices to memory.
// This allows for faster and more deterministic reading if cases of reading
// large files and when changing file offset by seeking around a lot.
// When mapping a file's index, the file system will be scanned for index pages
// and the info will be put in memory provided by user. When reading, the
// memory map can be looked up instead of searching for index pages on the
// medium. This way, user can trade memory against performance.
// Whole, parts of, or future parts not being written yet can be mapped. The
// memory array will be owned by psram and updated accordingly during garbage
// collecting or when modifying the indices. The latter is invoked by when the
// file is modified in some way. The index buffer is tied to the file
// descriptor.
#define PSRAMFS_IX_MAP                           1

// Set PSRAMFS_TEST_VISUALISATION to non-zero to enable PSRAMFS_vis function
// in the api. This function will visualize all filesystem using given printf
// function.
#ifdef CONFIG_PSRAMFS_TEST_VISUALISATION
#define PSRAMFS_TEST_VISUALISATION               1
#else
#define PSRAMFS_TEST_VISUALISATION               0
#endif
#if PSRAMFS_TEST_VISUALISATION
#ifndef psramfs_printf
#define psramfs_printf(...)                ESP_LOGD(PSRAMFS_TAG, __VA_ARGS__)
#endif
// psramfs_printf argument for a free page
#define PSRAMFS_TEST_VIS_FREE_STR          "_"
// psramfs_printf argument for a deleted page
#define PSRAMFS_TEST_VIS_DELE_STR          "/"
// psramfs_printf argument for an index page for given object id
#define PSRAMFS_TEST_VIS_INDX_STR(id)      "i"
// psramfs_printf argument for a data page for given object id
#define PSRAMFS_TEST_VIS_DATA_STR(id)      "d"
#endif

// Types depending on configuration such as the amount of flash bytes
// given to psram file system in total (psramfs_file_system_size),
// the logical block size (log_block_size), and the logical page size
// (log_page_size)

// Block index type. Make sure the size of this type can hold
// the highest number of all blocks - i.e. psramfs_file_system_size / log_block_size
typedef u16_t psramfs_block_ix;
// Page index type. Make sure the size of this type can hold
// the highest page number of all pages - i.e. psramfs_file_system_size / log_page_size
typedef u16_t psramfs_page_ix;
// Object id type - most significant bit is reserved for index flag. Make sure the
// size of this type can hold the highest object id on a full system,
// i.e. 2 + (psramfs_file_system_size / (2*log_page_size))*2
typedef u16_t psramfs_obj_id;
// Object span index type. Make sure the size of this type can
// hold the largest possible span index on the system -
// i.e. (psramfs_file_system_size / log_page_size) - 1
typedef u16_t psramfs_span_ix;

#endif /* PSRAMFS_CONFIG_H_ */
