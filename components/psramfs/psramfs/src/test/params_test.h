/*
 * params_test.h
 *
 *  Created on: May 26, 2013
 *      Author: petera
 */

#ifndef PARAMS_TEST_H_
#define PARAMS_TEST_H_

//////////////// TEST PARAMS ////////////////

// default test total emulated spi flash size
#define PHYS_FLASH_SIZE       (16*1024*1024)
// default test psram file system size
#define PSRAMFS_FLASH_SIZE     (2*1024*1024)
// default test psram file system offset in emulated spi flash
#define PSRAMFS_PHYS_ADDR      (4*1024*1024)
// default test sector size
#define SECTOR_SIZE         65536
// default test logical block size
#define LOG_BLOCK           (SECTOR_SIZE*2)
// default test logical page size
#define LOG_PAGE            (SECTOR_SIZE/256)
// default test number of filedescs
#define DEFAULT_NUM_FD            16
// default test number of cache pages
#define DEFAULT_NUM_CACHE_PAGES   8

// When testing, test bench create reference files for comparison on
// the actual hard drive. By default, put these on ram drive for speed.
#define TEST_PATH "/dev/shm/psram/test-data/"

#define ASSERT(c, m) real_assert((c),(m), __FILE__, __LINE__);
void real_assert(int c, const char *n, const char *file, int l);

/////////// PSRAMFS BUILD CONFIG  ////////////

// test using filesystem magic
#ifndef PSRAMFS_USE_MAGIC
#define PSRAMFS_USE_MAGIC    1
#endif
// test using filesystem magic length
#ifndef PSRAMFS_USE_MAGIC_LENGTH
#define PSRAMFS_USE_MAGIC_LENGTH   1
#endif
// test using extra param in callback
#ifndef PSRAMFS_HAL_CALLBACK_EXTRA
#define PSRAMFS_HAL_CALLBACK_EXTRA       1
#endif
// test using filehandle offset
#ifndef PSRAMFS_FILEHDL_OFFSET
#define PSRAMFS_FILEHDL_OFFSET           1
// use this offset
#define TEST_PSRAMFS_FILEHDL_OFFSET      0x1000
#endif

#ifdef NO_TEST
#define PSRAMFS_LOCK(fs)
#define PSRAMFS_UNLOCK(fs)
#else
struct psramfs_t;
extern void test_lock(struct psramfs_t *fs);
extern void test_unlock(struct psramfs_t *fs);
#define PSRAMFS_LOCK(fs)   test_lock(fs)
#define PSRAMFS_UNLOCK(fs) test_unlock(fs)
#endif

// dbg output
#define PSRAMFS_DBG(_f, ...) //printf("\x1b[32m" _f "\x1b[0m", ## __VA_ARGS__)
#define PSRAMFS_API_DBG(_f, ...) //printf("\n\x1b[1m\x1b[7m" _f "\x1b[0m", ## __VA_ARGS__)
#define PSRAMFS_GC_DBG(_f, ...) //printf("\x1b[36m" _f "\x1b[0m", ## __VA_ARGS__)
#define PSRAMFS_CACHE_DBG(_f, ...) //printf("\x1b[33m" _f "\x1b[0m", ## __VA_ARGS__)
#define PSRAMFS_CHECK_DBG(_f, ...) //printf("\x1b[31m" _f "\x1b[0m", ## __VA_ARGS__)

// needed types
typedef signed int s32_t;
typedef unsigned int u32_t;
typedef signed short s16_t;
typedef unsigned short u16_t;
typedef signed char s8_t;
typedef unsigned char u8_t;

#endif /* PARAMS_TEST_H_ */
