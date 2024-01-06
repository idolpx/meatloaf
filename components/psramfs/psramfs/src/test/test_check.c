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


SUITE(check_tests)
static void setup() {
  _setup();
}
static void teardown() {
  _teardown();
}

TEST(evil_write) {
  fs_set_validate_flashing(0);
  printf("writing corruption to block 1 data range (leaving lu intact)\n");
  u32_t data_range = PSRAMFS_CFG_LOG_BLOCK_SZ(FS) -
      PSRAMFS_CFG_LOG_PAGE_SZ(FS) * (PSRAMFS_OBJ_LOOKUP_PAGES(FS));
  u8_t *corruption = malloc(data_range);
  memrand(corruption, data_range);
  u32_t addr = 0 * PSRAMFS_CFG_LOG_PAGE_SZ(FS) * PSRAMFS_OBJ_LOOKUP_PAGES(FS);
  area_write(addr, corruption, data_range);
  free(corruption);

  int size = PSRAMFS_DATA_PAGE_SIZE(FS)*3;
  int res = test_create_and_write_file("file", size, size);

  printf("CHECK1-----------------\n");
  PSRAMFS_check(FS);
  printf("CHECK2-----------------\n");
  PSRAMFS_check(FS);
  printf("CHECK3-----------------\n");
  PSRAMFS_check(FS);

  res = test_create_and_write_file("file2", size, size);
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
} TEST_END


TEST(lu_check1) {
  int size = PSRAMFS_DATA_PAGE_SIZE(FS)*3;
  int res = test_create_and_write_file("file", size, size);
  TEST_CHECK(res >= 0);
  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  psramfs_file fd = PSRAMFS_open(FS, "file", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);
  psramfs_stat s;
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);

  // modify lu entry data page index 1
  psramfs_page_ix pix;
  res = psramfs_obj_lu_find_id_and_span(FS, s.obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG, 1, 0, &pix);
  TEST_CHECK(res >= 0);

  // reset lu entry to being erased, but keep page data
  psramfs_obj_id obj_id = PSRAMFS_OBJ_ID_DELETED;
  psramfs_block_ix bix = PSRAMFS_BLOCK_FOR_PAGE(FS, pix);
  int entry = PSRAMFS_OBJ_LOOKUP_ENTRY_FOR_PAGE(FS, pix);
  u32_t addr = PSRAMFS_BLOCK_TO_PADDR(FS, bix) + entry*sizeof(psramfs_obj_id);

  area_write(addr, (u8_t*)&obj_id, sizeof(psramfs_obj_id));

#if PSRAMFS_CACHE
  psramfs_cache *cache = psramfs_get_cache(FS);
  cache->cpage_use_map = 0;
#endif
  PSRAMFS_check(FS);

  return TEST_RES_OK;
} TEST_END


TEST(page_cons1) {
  int size = PSRAMFS_DATA_PAGE_SIZE(FS)*3;
  int res = test_create_and_write_file("file", size, size);
  TEST_CHECK(res >= 0);
  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  psramfs_file fd = PSRAMFS_open(FS, "file", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);
  psramfs_stat s;
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);

  // modify object index, find object index header
  psramfs_page_ix pix;
  res = psramfs_obj_lu_find_id_and_span(FS, s.obj_id | PSRAMFS_OBJ_ID_IX_FLAG, 0, 0, &pix);
  TEST_CHECK(res >= 0);

  // set object index entry 2 to a bad page
  u32_t addr = PSRAMFS_PAGE_TO_PADDR(FS, pix) + sizeof(psramfs_page_object_ix_header) + 0 * sizeof(psramfs_page_ix);
  psramfs_page_ix bad_pix_ref = 0x55;
  area_write(addr, (u8_t*)&bad_pix_ref, sizeof(psramfs_page_ix));
  area_write(addr + sizeof(psramfs_page_ix), (u8_t*)&bad_pix_ref, sizeof(psramfs_page_ix));

  // delete all cache
#if PSRAMFS_CACHE
  psramfs_cache *cache = psramfs_get_cache(FS);
  cache->cpage_use_map = 0;
#endif

  PSRAMFS_check(FS);

  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
} TEST_END


TEST(page_cons2) {
  int size = PSRAMFS_DATA_PAGE_SIZE(FS)*3;
  int res = test_create_and_write_file("file", size, size);
  TEST_CHECK(res >= 0);
  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  psramfs_file fd = PSRAMFS_open(FS, "file", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);
  psramfs_stat s;
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);

  // modify object index, find object index header
  psramfs_page_ix pix;
  res = psramfs_obj_lu_find_id_and_span(FS, s.obj_id | PSRAMFS_OBJ_ID_IX_FLAG, 0, 0, &pix);
  TEST_CHECK(res >= 0);

  // find data page span index 0
  psramfs_page_ix dpix;
  res = psramfs_obj_lu_find_id_and_span(FS, s.obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG, 0, 0, &dpix);
  TEST_CHECK(res >= 0);

  // set object index entry 1+2 to a data page 0
  u32_t addr = PSRAMFS_PAGE_TO_PADDR(FS, pix) + sizeof(psramfs_page_object_ix_header) + 1 * sizeof(psramfs_page_ix);
  psramfs_page_ix bad_pix_ref = dpix;
  area_write(addr, (u8_t*)&bad_pix_ref, sizeof(psramfs_page_ix));
  area_write(addr+sizeof(psramfs_page_ix), (u8_t*)&bad_pix_ref, sizeof(psramfs_page_ix));

  // delete all cache
#if PSRAMFS_CACHE
  psramfs_cache *cache = psramfs_get_cache(FS);
  cache->cpage_use_map = 0;
#endif

  PSRAMFS_check(FS);

  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
} TEST_END



TEST(page_cons3) {
  int size = PSRAMFS_DATA_PAGE_SIZE(FS)*3;
  int res = test_create_and_write_file("file", size, size);
  TEST_CHECK(res >= 0);
  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  psramfs_file fd = PSRAMFS_open(FS, "file", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);
  psramfs_stat s;
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);

  // modify object index, find object index header
  psramfs_page_ix pix;
  res = psramfs_obj_lu_find_id_and_span(FS, s.obj_id | PSRAMFS_OBJ_ID_IX_FLAG, 0, 0, &pix);
  TEST_CHECK(res >= 0);

  // set object index entry 1+2 lookup page
  u32_t addr = PSRAMFS_PAGE_TO_PADDR(FS, pix) + sizeof(psramfs_page_object_ix_header) + 1 * sizeof(psramfs_page_ix);
  psramfs_page_ix bad_pix_ref = PSRAMFS_PAGES_PER_BLOCK(FS) * (*FS.block_count - 2);
  area_write(addr, (u8_t*)&bad_pix_ref, sizeof(psramfs_page_ix));
  area_write(addr+sizeof(psramfs_page_ix), (u8_t*)&bad_pix_ref, sizeof(psramfs_page_ix));

  // delete all cache
#if PSRAMFS_CACHE
  psramfs_cache *cache = psramfs_get_cache(FS);
  cache->cpage_use_map = 0;
#endif

  PSRAMFS_check(FS);

  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
} TEST_END


TEST(page_cons_final) {
  int size = PSRAMFS_DATA_PAGE_SIZE(FS)*3;
  int res = test_create_and_write_file("file", size, size);
  TEST_CHECK(res >= 0);
  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  psramfs_file fd = PSRAMFS_open(FS, "file", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);
  psramfs_stat s;
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);

  // modify page header, make unfinalized
  psramfs_page_ix pix;
  res = psramfs_obj_lu_find_id_and_span(FS, s.obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG, 1, 0, &pix);
  TEST_CHECK(res >= 0);

  // set page span ix 1 as unfinalized
  u32_t addr = PSRAMFS_PAGE_TO_PADDR(FS, pix) + offsetof(psramfs_page_header, flags);
  u8_t flags;
  area_read(addr, (u8_t*)&flags, 1);
  flags |= PSRAMFS_PH_FLAG_FINAL;
  area_write(addr, (u8_t*)&flags, 1);

  // delete all cache
#if PSRAMFS_CACHE
  psramfs_cache *cache = psramfs_get_cache(FS);
  cache->cpage_use_map = 0;
#endif

  PSRAMFS_check(FS);

  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
} TEST_END


TEST(index_cons1) {
  int size = PSRAMFS_DATA_PAGE_SIZE(FS)*PSRAMFS_PAGES_PER_BLOCK(FS);
  int res = test_create_and_write_file("file", size, size);
  TEST_CHECK(res >= 0);
  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  psramfs_file fd = PSRAMFS_open(FS, "file", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);
  psramfs_stat s;
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);

  // modify lu entry data page index header
  psramfs_page_ix pix;
  res = psramfs_obj_lu_find_id_and_span(FS, s.obj_id | PSRAMFS_OBJ_ID_IX_FLAG, 0, 0, &pix);
  TEST_CHECK(res >= 0);

  printf("  deleting lu entry pix %04x\n", pix);
  // reset lu entry to being erased, but keep page data
  psramfs_obj_id obj_id = PSRAMFS_OBJ_ID_DELETED;
  psramfs_block_ix bix = PSRAMFS_BLOCK_FOR_PAGE(FS, pix);
  int entry = PSRAMFS_OBJ_LOOKUP_ENTRY_FOR_PAGE(FS, pix);
  u32_t addr = PSRAMFS_BLOCK_TO_PADDR(FS, bix) + entry * sizeof(psramfs_obj_id);

  area_write(addr, (u8_t*)&obj_id, sizeof(psramfs_obj_id));

#if PSRAMFS_CACHE
  psramfs_cache *cache = psramfs_get_cache(FS);
  cache->cpage_use_map = 0;
#endif
  PSRAMFS_check(FS);

  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
} TEST_END


TEST(index_cons2) {
  int size = PSRAMFS_DATA_PAGE_SIZE(FS)*PSRAMFS_PAGES_PER_BLOCK(FS);
  int res = test_create_and_write_file("file", size, size);
  TEST_CHECK(res >= 0);
  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  psramfs_file fd = PSRAMFS_open(FS, "file", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);
  psramfs_stat s;
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);

  // modify lu entry data page index header
  psramfs_page_ix pix;
  res = psramfs_obj_lu_find_id_and_span(FS, s.obj_id | PSRAMFS_OBJ_ID_IX_FLAG, 0, 0, &pix);
  TEST_CHECK(res >= 0);

  printf("  writing lu entry for index page, ix %04x, as data page\n", pix);
  psramfs_obj_id obj_id = 0x1234;
  psramfs_block_ix bix = PSRAMFS_BLOCK_FOR_PAGE(FS, pix);
  int entry = PSRAMFS_OBJ_LOOKUP_ENTRY_FOR_PAGE(FS, pix);
  u32_t addr = PSRAMFS_BLOCK_TO_PADDR(FS, bix) + entry * sizeof(psramfs_obj_id);

  area_write(addr, (u8_t*)&obj_id, sizeof(psramfs_obj_id));

#if PSRAMFS_CACHE
  psramfs_cache *cache = psramfs_get_cache(FS);
  cache->cpage_use_map = 0;
#endif
  PSRAMFS_check(FS);

  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
} TEST_END


TEST(index_cons3) {
  int size = PSRAMFS_DATA_PAGE_SIZE(FS)*PSRAMFS_PAGES_PER_BLOCK(FS);
  int res = test_create_and_write_file("file", size, size);
  TEST_CHECK(res >= 0);
  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  psramfs_file fd = PSRAMFS_open(FS, "file", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);
  psramfs_stat s;
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);

  // modify lu entry data page index header
  psramfs_page_ix pix;
  res = psramfs_obj_lu_find_id_and_span(FS, s.obj_id | PSRAMFS_OBJ_ID_IX_FLAG, 0, 0, &pix);
  TEST_CHECK(res >= 0);

  printf("  setting lu entry pix %04x to another index page\n", pix);
  // reset lu entry to being erased, but keep page data
  psramfs_obj_id obj_id = 1234 | PSRAMFS_OBJ_ID_IX_FLAG;
  psramfs_block_ix bix = PSRAMFS_BLOCK_FOR_PAGE(FS, pix);
  int entry = PSRAMFS_OBJ_LOOKUP_ENTRY_FOR_PAGE(FS, pix);
  u32_t addr = PSRAMFS_BLOCK_TO_PADDR(FS, bix) + entry * sizeof(psramfs_obj_id);

  area_write(addr, (u8_t*)&obj_id, sizeof(psramfs_obj_id));

#if PSRAMFS_CACHE
  psramfs_cache *cache = psramfs_get_cache(FS);
  cache->cpage_use_map = 0;
#endif
  PSRAMFS_check(FS);

  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
} TEST_END

TEST(index_cons4) {
  int size = PSRAMFS_DATA_PAGE_SIZE(FS)*PSRAMFS_PAGES_PER_BLOCK(FS);
  int res = test_create_and_write_file("file", size, size);
  TEST_CHECK(res >= 0);
  res = read_and_verify("file");
  TEST_CHECK(res >= 0);

  psramfs_file fd = PSRAMFS_open(FS, "file", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);
  psramfs_stat s;
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);

  // modify lu entry data page index header, flags
  psramfs_page_ix pix;
  res = psramfs_obj_lu_find_id_and_span(FS, s.obj_id | PSRAMFS_OBJ_ID_IX_FLAG, 0, 0, &pix);
  TEST_CHECK(res >= 0);

  printf("  cue objix hdr deletion in page %04x\n", pix);
  // set flags as deleting ix header
  u32_t addr = PSRAMFS_PAGE_TO_PADDR(FS, pix) + offsetof(psramfs_page_header, flags);
  u8_t flags = 0xff & ~(PSRAMFS_PH_FLAG_FINAL | PSRAMFS_PH_FLAG_USED | PSRAMFS_PH_FLAG_INDEX | PSRAMFS_PH_FLAG_IXDELE);

  area_write(addr, (u8_t*)&flags, 1);

#if PSRAMFS_CACHE
  psramfs_cache *cache = psramfs_get_cache(FS);
  cache->cpage_use_map = 0;
#endif
  PSRAMFS_check(FS);

  return TEST_RES_OK;
} TEST_END

SUITE_TESTS(check_tests)
  ADD_TEST(evil_write)
  ADD_TEST(lu_check1)
  ADD_TEST(page_cons1)
  ADD_TEST(page_cons2)
  ADD_TEST(page_cons3)
  ADD_TEST(page_cons_final)
  ADD_TEST(index_cons1)
  ADD_TEST(index_cons2)
  ADD_TEST(index_cons3)
  ADD_TEST(index_cons4)
SUITE_END(check_tests)
