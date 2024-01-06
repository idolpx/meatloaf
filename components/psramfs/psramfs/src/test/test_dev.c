/*
 * test_dev.c
 *
 *  Created on: Jul 14, 2013
 *      Author: petera
 */


#include "testrunner.h"
#include "test_psramfs.h"
#include "psramfs_nucleus.h"
#include "psram.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>


SUITE(dev_tests)
static void setup() {
  _setup();
}
static void teardown() {
  _teardown();
}

TEST(interrupted_write) {
  char *name = "interrupt";
  char *name2 = "interrupt2";
  int res;
  psramfs_file fd;

  const u32_t sz = PSRAMFS_CFG_LOG_PAGE_SZ(FS)*8;
  u8_t *buf = malloc(sz);
  memrand(buf, sz);

  printf("  create reference file\n");
  fd = PSRAMFS_open(FS, name, PSRAMFS_RDWR | PSRAMFS_CREAT | PSRAMFS_TRUNC, 0);
  TEST_CHECK(fd > 0);
  clear_flash_ops_log();
  res = PSRAMFS_write(FS, fd, buf, sz);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);

  u32_t written = get_flash_ops_log_write_bytes();
  printf("  written bytes: %i\n", written);


  printf("  create error file\n");
  fd = PSRAMFS_open(FS, name2, PSRAMFS_RDWR | PSRAMFS_CREAT | PSRAMFS_TRUNC, 0);
  TEST_CHECK(fd > 0);
  clear_flash_ops_log();
  invoke_error_after_write_bytes(written/2, 0);
  res = PSRAMFS_write(FS, fd, buf, sz);
  PSRAMFS_close(FS, fd);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_TEST);

  clear_flash_ops_log();

#if PSRAMFS_CACHE
  // delete all cache
  psramfs_cache *cache = psramfs_get_cache(FS);
  cache->cpage_use_map = 0;
#endif


  printf("  read error file\n");
  fd = PSRAMFS_open(FS, name2, PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);

  psramfs_stat s;
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  printf("  file size: %i\n", s.size);

  if (s.size > 0) {
    u8_t *buf2 = malloc(s.size);
    res = PSRAMFS_read(FS, fd, buf2, s.size);
    TEST_CHECK(res >= 0);

    u32_t ix = 0;
    for (ix = 0; ix < s.size; ix += 16) {
      int i;
      printf("  ");
      for (i = 0; i < 16; i++) {
        printf("%02x", buf[ix+i]);
      }
      printf("  ");
      for (i = 0; i < 16; i++) {
        printf("%02x", buf2[ix+i]);
      }
      printf("\n");
    }
    free(buf2);
  }
  PSRAMFS_close(FS, fd);


  printf("  FS check\n");
  PSRAMFS_check(FS);

  printf("  read error file again\n");
  fd = PSRAMFS_open(FS, name2, PSRAMFS_APPEND | PSRAMFS_RDWR, 0);
  TEST_CHECK(fd > 0);
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  printf("  file size: %i\n", s.size);
  printf("  write file\n");
  res = PSRAMFS_write(FS, fd, buf, sz);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);

  free(buf);

  return TEST_RES_OK;

} TEST_END

SUITE_TESTS(dev_tests)
  ADD_TEST(interrupted_write)
SUITE_END(dev_tests)
