/*
 * test_suites.c
 *
 *  Created on: Jun 19, 2013
 *      Author: petera
 */


#include "testrunner.h"
#include "test_psramfs.h"
#include "psramfs_nucleus.h"
#include "psramfs.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

SUITE(hydrogen_tests)
static void setup() {
  _setup();
}
static void teardown() {
  _teardown();
}

TEST(info)
{
  u32_t used, total;
  int res = PSRAMFS_info(FS, &total, &used);
  TEST_CHECK(res == PSRAMFS_OK);
  TEST_CHECK(used == 0);
  TEST_CHECK(total < PSRAMFS_CFG_PHYS_SZ(&__fs));
  return TEST_RES_OK;
}
TEST_END

#if PSRAMFS_USE_MAGIC
TEST(magic)
{
  fs_reset_specific(0, 0, 65536*16, 65536, 65536, 256);
  PSRAMFS_unmount(FS);

  TEST_CHECK_EQ(fs_mount_specific(0, 65536*16, 65536, 65536, 256), PSRAMFS_OK);
  PSRAMFS_unmount(FS);

  TEST_CHECK_NEQ(fs_mount_specific(0, 65536*16, 65536, 65536, 128), PSRAMFS_OK);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NOT_A_FS);

  TEST_CHECK_NEQ(fs_mount_specific(4, 65536*16, 65536, 65536, 256), PSRAMFS_OK);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NOT_A_FS);

  return TEST_RES_OK;
}
TEST_END


#if PSRAMFS_USE_MAGIC_LENGTH
TEST(magic_length)
{
  fs_reset_specific(0, 0, 65536*16, 65536, 65536, 256);
  PSRAMFS_unmount(FS);

  TEST_CHECK_EQ(fs_mount_specific(0, 65536*16, 65536, 65536, 256), PSRAMFS_OK);
  PSRAMFS_unmount(FS);

  TEST_CHECK_NEQ(fs_mount_specific(0, 65536*8, 65536, 65536, 256), PSRAMFS_OK);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NOT_A_FS);

  TEST_CHECK_NEQ(fs_mount_specific(0, 65536*15, 65536, 65536, 256), PSRAMFS_OK);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NOT_A_FS);

  TEST_CHECK_NEQ(fs_mount_specific(0, 65536*17, 65536, 65536, 256), PSRAMFS_OK);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NOT_A_FS);

  TEST_CHECK_NEQ(fs_mount_specific(0, 65536*256, 65536, 65536, 256), PSRAMFS_OK);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NOT_A_FS);

  return TEST_RES_OK;
}
TEST_END

#if PSRAMFS_SINGLETON==0
TEST(magic_length_probe)
{
  fs_reset_specific(0, 0, 65536*16, 65536, 65536, 256);
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), 65536*16);

  fs_reset_specific(0, 0, 65536*24, 65536, 65536, 256);
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), 65536*24);

  fs_reset_specific(0, 0, 32768*16, 32768, 32768, 128);
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), 32768*16);

  fs_reset_specific(0, 0, 16384*37, 16384, 16384, 128);
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), 16384*37);

  fs_reset_specific(0, 0, 4096*11, 4096, 4096, 256);
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), 4096*11);

  fs_reset_specific(0, 0, 4096*8, 4096, 4096, 256);
  __fs.cfg.log_page_size = 128;
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), PSRAMFS_ERR_PROBE_NOT_A_FS);
  __fs.cfg.log_page_size = 512;
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), PSRAMFS_ERR_PROBE_NOT_A_FS);
  __fs.cfg.log_page_size = 256;
  __fs.cfg.log_block_size = 8192;
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), PSRAMFS_ERR_PROBE_NOT_A_FS);
  __fs.cfg.log_block_size = 2048;
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), PSRAMFS_ERR_PROBE_NOT_A_FS);
  __fs.cfg.log_block_size = 4096;
  __fs.cfg.phys_addr += 2;
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), PSRAMFS_ERR_PROBE_NOT_A_FS);

  fs_reset_specific(0, 0, 4096*8, 4096, 4096, 256);
  __fs.cfg.phys_addr += 4096*6;
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), PSRAMFS_ERR_PROBE_TOO_FEW_BLOCKS);

  fs_reset_specific(0, 0, 4096*8, 4096, 4096, 256);
  area_set(4096*0, 0xff, 4096); // "erase" block 0
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), 4096*8);

  fs_reset_specific(0, 0, 4096*8, 4096, 4096, 256);
  area_set(4096*0, 0xff, 4096); // "erase" block 1
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), 4096*8);

  fs_reset_specific(0, 0, 4096*8, 4096, 4096, 256);
  area_set(4096*0, 0xff, 4096); // "erase" block 2
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), 4096*8);

  fs_reset_specific(0, 0, 4096*8, 4096, 4096, 256);
  area_set(4096*0, 0xff, 4096*2); // "erase" block 0 & 1
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), PSRAMFS_ERR_PROBE_NOT_A_FS);

  fs_reset_specific(0, 0, 4096*8, 4096, 4096, 256);
  area_set(4096*0, 0xff, 4096*2); // "erase" block 0
  area_set(4096*0, 0xff, 4096); // "erase" block 2
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), PSRAMFS_ERR_PROBE_NOT_A_FS);

  fs_reset_specific(0, 0, 4096*8, 4096, 4096, 256);
  area_set(4096*1, 0xff, 4096*2); // "erase" block 1 & 2
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), PSRAMFS_ERR_PROBE_NOT_A_FS);

  fs_reset_specific(0, 0, 4096*8, 4096, 4096, 256);
  area_set(4096*0, 0xff, 4096*8); // "erase" all
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), PSRAMFS_ERR_PROBE_NOT_A_FS);

  fs_reset_specific(0, 0, 4096*8, 4096, 4096, 256);
  area_set(4096*0, 0xdd, 4096*8); // garble all
  TEST_CHECK_EQ(PSRAMFS_probe_fs(&__fs.cfg), PSRAMFS_ERR_PROBE_NOT_A_FS);

  return TEST_RES_OK;
}
TEST_END

#endif // PSRAMFS_SINGLETON==0

#endif // PSRAMFS_USE_MAGIC_LENGTH

#endif // PSRAMFS_USE_MAGIC

TEST(missing_file)
{
  psramfs_file fd = PSRAMFS_open(FS, "this_wont_exist", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd < 0);
  return TEST_RES_OK;
}
TEST_END


TEST(bad_fd)
{
  int res;
  psramfs_stat s;
  psramfs_file fd = PSRAMFS_open(FS, "this_wont_exist", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd < 0);
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_BAD_DESCRIPTOR);
  res = PSRAMFS_fremove(FS, fd);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_BAD_DESCRIPTOR);
  res = PSRAMFS_lseek(FS, fd, 0, PSRAMFS_SEEK_CUR);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_BAD_DESCRIPTOR);
  res = PSRAMFS_read(FS, fd, 0, 0);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_BAD_DESCRIPTOR);
  res = PSRAMFS_write(FS, fd, 0, 0);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_BAD_DESCRIPTOR);
#if PSRAMFS_OBJ_META_LEN
  u8_t new_meta[PSRAMFS_OBJ_META_LEN] = {0};
  res = PSRAMFS_fupdate_meta(FS, fd, new_meta);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_BAD_DESCRIPTOR);
#endif
  return TEST_RES_OK;
}
TEST_END


TEST(closed_fd)
{
  int res;
  psramfs_stat s;
  res = test_create_file("file");
  psramfs_file fd = PSRAMFS_open(FS, "file", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd >= 0);
  PSRAMFS_close(FS, fd);

  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
  res = PSRAMFS_fremove(FS, fd);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
  res = PSRAMFS_lseek(FS, fd, 0, PSRAMFS_SEEK_CUR);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
  res = PSRAMFS_read(FS, fd, 0, 0);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
  res = PSRAMFS_write(FS, fd, 0, 0);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
#if PSRAMFS_OBJ_META_LEN
  u8_t new_meta[PSRAMFS_OBJ_META_LEN] = {0};
  res = PSRAMFS_fupdate_meta(FS, fd, new_meta);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
#endif
  return TEST_RES_OK;
}
TEST_END


TEST(deleted_same_fd)
{
  int res;
  psramfs_stat s;
  psramfs_file fd;
  res = test_create_file("remove");
  fd = PSRAMFS_open(FS, "remove", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd >= 0);
  fd = PSRAMFS_open(FS, "remove", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd >= 0);
  res = PSRAMFS_fremove(FS, fd);
  TEST_CHECK(res >= 0);

  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
  res = PSRAMFS_fremove(FS, fd);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
  res = PSRAMFS_lseek(FS, fd, 0, PSRAMFS_SEEK_CUR);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
  res = PSRAMFS_read(FS, fd, 0, 0);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
  res = PSRAMFS_write(FS, fd, 0, 0);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);

  return TEST_RES_OK;
}
TEST_END


TEST(deleted_other_fd)
{
  int res;
  psramfs_stat s;
  psramfs_file fd, fd_orig;
  res = test_create_file("remove");
  fd_orig = PSRAMFS_open(FS, "remove", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd_orig >= 0);
  fd = PSRAMFS_open(FS, "remove", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd >= 0);
  res = PSRAMFS_fremove(FS, fd_orig);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd_orig);

  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
  res = PSRAMFS_fremove(FS, fd);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
  res = PSRAMFS_lseek(FS, fd, 0, PSRAMFS_SEEK_CUR);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
  res = PSRAMFS_read(FS, fd, 0, 0);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);
  res = PSRAMFS_write(FS, fd, 0, 0);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_CLOSED);

  return TEST_RES_OK;
}
TEST_END


TEST(file_by_open)
{
  int res;
  psramfs_stat s;
  psramfs_file fd = PSRAMFS_open(FS, "filebopen", PSRAMFS_CREAT, 0);
  TEST_CHECK(fd >= 0);
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  TEST_CHECK(strcmp((char*)s.name, "filebopen") == 0);
  TEST_CHECK(s.size == 0);
  PSRAMFS_close(FS, fd);

  fd = PSRAMFS_open(FS, "filebopen", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd >= 0);
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  TEST_CHECK(strcmp((char*)s.name, "filebopen") == 0);
  TEST_CHECK(s.size == 0);
  PSRAMFS_close(FS, fd);
  return TEST_RES_OK;
}
TEST_END


TEST(file_by_creat)
{
  int res;
  res = test_create_file("filebcreat");
  TEST_CHECK(res >= 0);
  res = PSRAMFS_creat(FS, "filebcreat", 0);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS)==PSRAMFS_ERR_CONFLICTING_NAME);
  return TEST_RES_OK;
}
TEST_END

TEST(file_by_open_excl)
{
  int res;
  psramfs_stat s;
  psramfs_file fd = PSRAMFS_open(FS, "filebexcl", PSRAMFS_CREAT | PSRAMFS_EXCL, 0);
  TEST_CHECK(fd >= 0);
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK(res >= 0);
  TEST_CHECK(strcmp((char*)s.name, "filebexcl") == 0);
  TEST_CHECK(s.size == 0);
  PSRAMFS_close(FS, fd);

  fd = PSRAMFS_open(FS, "filebexcl", PSRAMFS_CREAT | PSRAMFS_EXCL, 0);
  TEST_CHECK(fd < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_FILE_EXISTS);

  return TEST_RES_OK;
}
TEST_END

#if PSRAMFS_FILEHDL_OFFSET
TEST(open_fh_offs)
{
  psramfs_file fd1, fd2, fd3;
  fd1 = PSRAMFS_open(FS, "1", PSRAMFS_CREAT | PSRAMFS_EXCL, 0);
  fd2 = PSRAMFS_open(FS, "2", PSRAMFS_CREAT | PSRAMFS_EXCL, 0);
  fd3 = PSRAMFS_open(FS, "3", PSRAMFS_CREAT | PSRAMFS_EXCL, 0);
  TEST_CHECK(fd1 >= TEST_PSRAMFS_FILEHDL_OFFSET);
  TEST_CHECK(fd2 >= TEST_PSRAMFS_FILEHDL_OFFSET);
  TEST_CHECK(fd3 >= TEST_PSRAMFS_FILEHDL_OFFSET);
  PSRAMFS_close(FS, fd1);
  fd1 = PSRAMFS_open(FS, "2", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd1 >= TEST_PSRAMFS_FILEHDL_OFFSET);
  PSRAMFS_close(FS, fd2);
  fd2 = PSRAMFS_open(FS, "3", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd2 >= TEST_PSRAMFS_FILEHDL_OFFSET);
  PSRAMFS_close(FS, fd3);
  fd3 = PSRAMFS_open(FS, "1", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd3 >= TEST_PSRAMFS_FILEHDL_OFFSET);
  PSRAMFS_close(FS, fd1);
  PSRAMFS_close(FS, fd2);
  PSRAMFS_close(FS, fd3);
  fd1 = PSRAMFS_open(FS, "3", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd1 >= TEST_PSRAMFS_FILEHDL_OFFSET);
  PSRAMFS_close(FS, fd1);
  fd1 = PSRAMFS_open(FS, "foo", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd1 < TEST_PSRAMFS_FILEHDL_OFFSET);

  return TEST_RES_OK;
}
TEST_END

#endif //PSRAMFS_FILEHDL_OFFSET

TEST(list_dir)
{
  int res;

  char *files[4] = {
      "file1",
      "file2",
      "file3",
      "file4"
  };
  int file_cnt = sizeof(files)/sizeof(char *);

  int i;

  for (i = 0; i < file_cnt; i++) {
    res = test_create_file(files[i]);
    TEST_CHECK(res >= 0);
  }

  psramfs_DIR d;
  struct psramfs_dirent e;
  struct psramfs_dirent *pe = &e;

  PSRAMFS_opendir(FS, "/", &d);
  int found = 0;
  while ((pe = PSRAMFS_readdir(&d, pe))) {
    printf("  %s [%04x] size:%i\n", pe->name, pe->obj_id, pe->size);
    for (i = 0; i < file_cnt; i++) {
      if (strcmp(files[i], (char *)pe->name) == 0) {
        found++;
        break;
      }
    }
    {
      psramfs_stat s;
      TEST_CHECK_EQ(PSRAMFS_stat(FS, pe->name, &s), 0);
      TEST_CHECK_EQ(pe->obj_id, s.obj_id);
      TEST_CHECK_EQ(pe->size, s.size);
      TEST_CHECK_EQ(pe->type, s.type);
      TEST_CHECK_EQ(pe->pix, s.pix);
#if PSRAMFS_OBJ_META_LEN
      TEST_CHECK_EQ(memcmp(pe->meta, s.meta, PSRAMFS_OBJ_META_LEN), 0);
#endif
    }
  }
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_OK);
  PSRAMFS_closedir(&d);

  TEST_CHECK(found == file_cnt);

  return TEST_RES_OK;
}
TEST_END


TEST(open_by_dirent) {
  int res;

  char *files[4] = {
      "file1",
      "file2",
      "file3",
      "file4"
  };
  int file_cnt = sizeof(files)/sizeof(char *);

  int i;
  int size = PSRAMFS_DATA_PAGE_SIZE(FS);

  for (i = 0; i < file_cnt; i++) {
    res = test_create_and_write_file(files[i], size, size);
    TEST_CHECK(res >= 0);
  }

  psramfs_DIR d;
  struct psramfs_dirent e;
  struct psramfs_dirent *pe = &e;

  int found = 0;
  PSRAMFS_opendir(FS, "/", &d);
  while ((pe = PSRAMFS_readdir(&d, pe))) {
    psramfs_file fd = PSRAMFS_open_by_dirent(FS, pe, PSRAMFS_RDWR, 0);
    TEST_CHECK(fd >= 0);
    res = read_and_verify_fd(fd, (char *)pe->name);
    TEST_CHECK(res == PSRAMFS_OK);
    fd = PSRAMFS_open_by_dirent(FS, pe, PSRAMFS_RDWR, 0);
    TEST_CHECK(fd >= 0);
    res = PSRAMFS_fremove(FS, fd);
    TEST_CHECK(res == PSRAMFS_OK);
    PSRAMFS_close(FS, fd);
    found++;
  }
  PSRAMFS_closedir(&d);

  TEST_CHECK(found == file_cnt);

  found = 0;
  PSRAMFS_opendir(FS, "/", &d);
  while ((pe = PSRAMFS_readdir(&d, pe))) {
    found++;
  }
  PSRAMFS_closedir(&d);

  TEST_CHECK(found == 0);

  return TEST_RES_OK;

} TEST_END


TEST(open_by_page) {
  int res;

  char *files[4] = {
      "file1",
      "file2",
      "file3",
      "file4"
  };
  int file_cnt = sizeof(files)/sizeof(char *);

  int i;
  int size = PSRAMFS_DATA_PAGE_SIZE(FS);

  for (i = 0; i < file_cnt; i++) {
    res = test_create_and_write_file(files[i], size, size);
    TEST_CHECK(res >= 0);
  }

  psramfs_DIR d;
  struct psramfs_dirent e;
  struct psramfs_dirent *pe = &e;

  int found = 0;
  PSRAMFS_opendir(FS, "/", &d);
  while ((pe = PSRAMFS_readdir(&d, pe))) {
    psramfs_file fd = PSRAMFS_open_by_dirent(FS, pe, PSRAMFS_RDWR, 0);
    TEST_CHECK(fd >= 0);
    res = read_and_verify_fd(fd, (char *)pe->name);
    TEST_CHECK(res == PSRAMFS_OK);
    fd = PSRAMFS_open_by_page(FS, pe->pix, PSRAMFS_RDWR, 0);
    TEST_CHECK(fd >= 0);
    res = PSRAMFS_fremove(FS, fd);
    TEST_CHECK(res == PSRAMFS_OK);
    PSRAMFS_close(FS, fd);
    found++;
  }
  PSRAMFS_closedir(&d);

  TEST_CHECK(found == file_cnt);

  found = 0;
  PSRAMFS_opendir(FS, "/", &d);
  while ((pe = PSRAMFS_readdir(&d, pe))) {
    found++;
  }
  PSRAMFS_closedir(&d);

  TEST_CHECK(found == 0);

  psramfs_file fd;
  // test opening a lookup page
  fd = PSRAMFS_open_by_page(FS, 0, PSRAMFS_RDWR, 0);
  TEST_CHECK_LT(fd, 0);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NOT_A_FILE);
  // test opening a proper page but not being object index
  fd = PSRAMFS_open_by_page(FS, PSRAMFS_OBJ_LOOKUP_PAGES(FS)+1, PSRAMFS_RDWR, 0);
  TEST_CHECK_LT(fd, 0);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NOT_A_FILE);

  return TEST_RES_OK;
} TEST_END


static struct {
  u32_t calls;
  psramfs_fileop_type op;
  psramfs_obj_id obj_id;
  psramfs_page_ix pix;
} ucb;

void test_cb(psram *fs, psramfs_fileop_type op, psramfs_obj_id obj_id, psramfs_page_ix pix) {
  ucb.calls++;
  ucb.op = op;
  ucb.obj_id = obj_id;
  ucb.pix = pix;
  //printf("%4i  op:%i objid:%04x pix:%04x\n", ucb.calls, ucb.op, ucb.obj_id, ucb.pix);
}

TEST(user_callback_basic) {
  PSRAMFS_set_file_callback_func(FS, test_cb);
  int res;
  memset(&ucb, 0, sizeof(ucb));
  psramfs_file fd = PSRAMFS_open(FS, "foo", PSRAMFS_CREAT | PSRAMFS_APPEND | PSRAMFS_RDWR, 0);
  TEST_CHECK_GE(fd, 0);
  TEST_CHECK_EQ(ucb.calls, 1);
  TEST_CHECK_EQ(ucb.op, PSRAMFS_CB_CREATED);
  psramfs_stat s;
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK_GE(res, 0);
  TEST_CHECK_EQ(ucb.obj_id, s.obj_id);
  TEST_CHECK_EQ(ucb.pix, s.pix);

  res = PSRAMFS_write(FS, fd, "howdy partner", 14);
  TEST_CHECK_GE(res, 0);
  res = PSRAMFS_fflush(FS, fd);
  TEST_CHECK_GE(res, 0);
  TEST_CHECK_EQ(ucb.calls, 2);
  TEST_CHECK_EQ(ucb.op, PSRAMFS_CB_UPDATED);
  TEST_CHECK_EQ(ucb.obj_id, s.obj_id);
  TEST_CHECK_EQ(ucb.pix, s.pix);

  res = PSRAMFS_fremove(FS, fd);
  TEST_CHECK_GE(res, 0);
  TEST_CHECK_EQ(ucb.calls, 3);
  TEST_CHECK_EQ(ucb.op, PSRAMFS_CB_DELETED);
  TEST_CHECK_EQ(ucb.obj_id, s.obj_id);
  TEST_CHECK_EQ(ucb.pix, s.pix);

  return TEST_RES_OK;
} TEST_END


TEST(user_callback_gc) {
  PSRAMFS_set_file_callback_func(FS, test_cb);

  char name[32];
  int f;
  int size = PSRAMFS_DATA_PAGE_SIZE(FS);
  int pages_per_block = PSRAMFS_PAGES_PER_BLOCK(FS) - PSRAMFS_OBJ_LOOKUP_PAGES(FS);
  int files = (pages_per_block-1)/2;
  int res;

  // fill block with files
  for (f = 0; f < files; f++) {
    sprintf(name, "file%i", f);
    res = test_create_and_write_file(name, size, 1);
    TEST_CHECK(res >= 0);
  }
  for (f = 0; f < files; f++) {
    sprintf(name, "file%i", f);
    res = read_and_verify(name);
    TEST_CHECK(res >= 0);
  }
  // remove all files in block
  for (f = 0; f < files; f++) {
    sprintf(name, "file%i", f);
    res = PSRAMFS_remove(FS, name);
    TEST_CHECK(res >= 0);
  }

  memset(&ucb, 0, sizeof(ucb));
  psramfs_file fd = PSRAMFS_open(FS, "foo", PSRAMFS_CREAT | PSRAMFS_APPEND | PSRAMFS_RDWR, 0);
  TEST_CHECK_GE(fd, 0);
  TEST_CHECK_EQ(ucb.calls, 1);
  TEST_CHECK_EQ(ucb.op, PSRAMFS_CB_CREATED);
  psramfs_stat s;
  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK_GE(res, 0);
  TEST_CHECK_EQ(ucb.obj_id, s.obj_id);
  TEST_CHECK_EQ(ucb.pix, s.pix);

  res = PSRAMFS_write(FS, fd, "howdy partner", 14);
  TEST_CHECK_GE(res, 0);
  res = PSRAMFS_fflush(FS, fd);
  TEST_CHECK_GE(res, 0);
  TEST_CHECK_EQ(ucb.calls, 2);
  TEST_CHECK_EQ(ucb.op, PSRAMFS_CB_UPDATED);
  TEST_CHECK_EQ(ucb.obj_id, s.obj_id);
  TEST_CHECK_EQ(ucb.pix, s.pix);

  u32_t tot, us;
  PSRAMFS_info(FS, &tot, &us);

  // do a hard gc, should move our file
  res = PSRAMFS_gc(FS, tot-us*2);
  TEST_CHECK_GE(res, 0);

  TEST_CHECK_GE(res, 0);
  TEST_CHECK_EQ(ucb.calls, 3);
  TEST_CHECK_EQ(ucb.op, PSRAMFS_CB_UPDATED);
  TEST_CHECK_EQ(ucb.obj_id, s.obj_id);
  TEST_CHECK_NEQ(ucb.pix, s.pix);

  res = PSRAMFS_fstat(FS, fd, &s);
  TEST_CHECK_GE(res, 0);
  TEST_CHECK_EQ(ucb.pix, s.pix);

  res = PSRAMFS_fremove(FS, fd);
  TEST_CHECK_GE(res, 0);
  TEST_CHECK_EQ(ucb.calls, 4);
  TEST_CHECK_EQ(ucb.op, PSRAMFS_CB_DELETED);
  TEST_CHECK_EQ(ucb.obj_id, s.obj_id);
  TEST_CHECK_EQ(ucb.pix, s.pix);

  return TEST_RES_OK;
} TEST_END


TEST(name_too_long) {
  char name[PSRAMFS_OBJ_NAME_LEN*2];
  memset(name, 0, sizeof(name));
  int i;
  for (i = 0; i < PSRAMFS_OBJ_NAME_LEN; i++) {
    name[i] = 'A';
  }

  TEST_CHECK_LT(PSRAMFS_creat(FS, name, 0), PSRAMFS_OK);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NAME_TOO_LONG);

  TEST_CHECK_LT(PSRAMFS_open(FS, name, PSRAMFS_CREAT | PSRAMFS_TRUNC, 0), PSRAMFS_OK);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NAME_TOO_LONG);

  TEST_CHECK_LT(PSRAMFS_remove(FS, name), PSRAMFS_OK);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NAME_TOO_LONG);

  psramfs_stat s;
  TEST_CHECK_LT(PSRAMFS_stat(FS, name, &s), PSRAMFS_OK);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NAME_TOO_LONG);

  TEST_CHECK_LT(PSRAMFS_rename(FS, name, "a"), PSRAMFS_OK);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NAME_TOO_LONG);

  TEST_CHECK_LT(PSRAMFS_rename(FS, "a", name), PSRAMFS_OK);
  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_NAME_TOO_LONG);

  return TEST_RES_OK;
} TEST_END


TEST(rename) {
  int res;

  char *src_name = "baah";
  char *dst_name = "booh";
  char *dst_name2 = "beeh";
  int size = PSRAMFS_DATA_PAGE_SIZE(FS);

  res = test_create_and_write_file(src_name, size, size);
  TEST_CHECK(res >= 0);

  res = PSRAMFS_rename(FS, src_name, dst_name);
  TEST_CHECK(res >= 0);

  res = PSRAMFS_rename(FS, dst_name, dst_name);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_CONFLICTING_NAME);

  res = PSRAMFS_rename(FS, src_name, dst_name2);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_NOT_FOUND);

  return TEST_RES_OK;
} TEST_END

#if PSRAMFS_OBJ_META_LEN
TEST(update_meta) {
  s32_t i, res, fd;
  psramfs_stat s;
  u8_t new_meta[PSRAMFS_OBJ_META_LEN], new_meta2[PSRAMFS_OBJ_META_LEN];

  res = test_create_file("foo");
  TEST_CHECK(res >= 0);

  for (i = 0; i < PSRAMFS_OBJ_META_LEN; i++) {
    new_meta[i] = 0xaa;
  }
  res = PSRAMFS_update_meta(FS, "foo", new_meta);
  TEST_CHECK(res >= 0);

  res = PSRAMFS_stat(FS, "foo", &s);
  TEST_CHECK(res >= 0);
  TEST_CHECK_EQ(memcmp(s.meta, new_meta, PSRAMFS_OBJ_META_LEN), 0);

  for (i = 0; i < PSRAMFS_OBJ_META_LEN; i++) {
    new_meta2[i] = 0xbb;
  }

  fd = PSRAMFS_open(FS, "foo", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd >= 0);
  res = PSRAMFS_fupdate_meta(FS, fd, new_meta2);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_NOT_WRITABLE);
  PSRAMFS_close(FS, fd);

  res = PSRAMFS_stat(FS, "foo", &s);
  TEST_CHECK(res >= 0);
  TEST_CHECK_EQ(memcmp(s.meta, new_meta, PSRAMFS_OBJ_META_LEN), 0);

  fd = PSRAMFS_open(FS, "foo", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd >= 0);
  res = PSRAMFS_fupdate_meta(FS, fd, new_meta2);
  TEST_CHECK_EQ(res, 0);
  PSRAMFS_close(FS, fd);

  res = PSRAMFS_stat(FS, "foo", &s);
  TEST_CHECK(res >= 0);
  TEST_CHECK_EQ(memcmp(s.meta, new_meta2, PSRAMFS_OBJ_META_LEN), 0);

  return TEST_RES_OK;
} TEST_END
#endif

TEST(remove_single_by_path)
{
  int res;
  psramfs_file fd;
  res = test_create_file("remove");
  TEST_CHECK(res >= 0);
  res = PSRAMFS_remove(FS, "remove");
  TEST_CHECK(res >= 0);
  fd = PSRAMFS_open(FS, "remove", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_NOT_FOUND);

  return TEST_RES_OK;
}
TEST_END


TEST(remove_single_by_fd)
{
  int res;
  psramfs_file fd;
  res = test_create_file("remove");
  TEST_CHECK(res >= 0);
  fd = PSRAMFS_open(FS, "remove", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd >= 0);
  res = PSRAMFS_fremove(FS, fd);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);
  fd = PSRAMFS_open(FS, "remove", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_NOT_FOUND);

  return TEST_RES_OK;
}
TEST_END


TEST(write_cache)
{
  int res;
  psramfs_file fd;
  u8_t buf[1024*8];
  u8_t fbuf[1024*8];
  res = test_create_file("f");
  TEST_CHECK(res >= 0);
  fd = PSRAMFS_open(FS, "f", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd >= 0);
  memrand(buf, sizeof(buf));
  res = PSRAMFS_write(FS, fd, buf, PSRAMFS_CFG_LOG_PAGE_SZ(FS)/2);
  TEST_CHECK(res >= 0);
  res = PSRAMFS_write(FS, fd, buf, PSRAMFS_CFG_LOG_PAGE_SZ(FS)*2);
  TEST_CHECK(res >= 0);
  res = PSRAMFS_close(FS, fd);
  TEST_CHECK(res >= 0);

  fd = PSRAMFS_open(FS, "f", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd >= 0);
  res = PSRAMFS_read(FS, fd, fbuf, PSRAMFS_CFG_LOG_PAGE_SZ(FS)/2 + PSRAMFS_CFG_LOG_PAGE_SZ(FS)*2);
  TEST_CHECK(res >= 0);
  TEST_CHECK(0 == memcmp(&buf[0], &fbuf[0], PSRAMFS_CFG_LOG_PAGE_SZ(FS)/2));
  TEST_CHECK(0 == memcmp(&buf[0], &fbuf[PSRAMFS_CFG_LOG_PAGE_SZ(FS)/2], PSRAMFS_CFG_LOG_PAGE_SZ(FS)*2));
  res = PSRAMFS_close(FS, fd);
  TEST_CHECK(res >= 0);

  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_OK);

  return TEST_RES_OK;
}
TEST_END


TEST(write_big_file_chunks_page)
{
  int size = ((50*PSRAMFS_CFG_PHYS_SZ(FS))/100);
  printf("  filesize %i\n", size);
  int res = test_create_and_write_file("bigfile", size, PSRAMFS_DATA_PAGE_SIZE(FS));
  TEST_CHECK(res >= 0);
  res = read_and_verify("bigfile");
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
}
TEST_END


TEST(write_big_files_chunks_page)
{
  char name[32];
  int f;
  int files = 10;
  int res;
  int size = ((50*PSRAMFS_CFG_PHYS_SZ(FS))/100)/files;
  printf("  filesize %i\n", size);
  for (f = 0; f < files; f++) {
    sprintf(name, "bigfile%i", f);
    res = test_create_and_write_file(name, size, PSRAMFS_DATA_PAGE_SIZE(FS));
    TEST_CHECK(res >= 0);
  }
  for (f = 0; f < files; f++) {
    sprintf(name, "bigfile%i", f);
    res = read_and_verify(name);
    TEST_CHECK(res >= 0);
  }

  return TEST_RES_OK;
}
TEST_END


TEST(write_big_file_chunks_index)
{
  int size = ((50*PSRAMFS_CFG_PHYS_SZ(FS))/100);
  printf("  filesize %i\n", size);
  int res = test_create_and_write_file("bigfile", size, PSRAMFS_DATA_PAGE_SIZE(FS) * PSRAMFS_OBJ_HDR_IX_LEN(FS));
  TEST_CHECK(res >= 0);
  res = read_and_verify("bigfile");
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
}
TEST_END


TEST(write_big_files_chunks_index)
{
  char name[32];
  int f;
  int files = 10;
  int res;
  int size = ((50*PSRAMFS_CFG_PHYS_SZ(FS))/100)/files;
  printf("  filesize %i\n", size);
  for (f = 0; f < files; f++) {
    sprintf(name, "bigfile%i", f);
    res = test_create_and_write_file(name, size, PSRAMFS_DATA_PAGE_SIZE(FS) * PSRAMFS_OBJ_HDR_IX_LEN(FS));
    TEST_CHECK(res >= 0);
  }
  for (f = 0; f < files; f++) {
    sprintf(name, "bigfile%i", f);
    res = read_and_verify(name);
    TEST_CHECK(res >= 0);
  }

  return TEST_RES_OK;
}
TEST_END


TEST(write_big_file_chunks_huge)
{
  int size = (FS_PURE_DATA_PAGES(FS) / 2) * PSRAMFS_DATA_PAGE_SIZE(FS);
  printf("  filesize %i\n", size);
  int res = test_create_and_write_file("bigfile", size, size);
  TEST_CHECK(res >= 0);
  res = read_and_verify("bigfile");
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
}
TEST_END


TEST(write_big_files_chunks_huge)
{
  char name[32];
  int f;
  int files = 10;
  int res;
  int size = ((50*PSRAMFS_CFG_PHYS_SZ(FS))/100)/files;
  printf("  filesize %i\n", size);
  for (f = 0; f < files; f++) {
    sprintf(name, "bigfile%i", f);
    res = test_create_and_write_file(name, size, size);
    TEST_CHECK(res >= 0);
  }
  for (f = 0; f < files; f++) {
    sprintf(name, "bigfile%i", f);
    res = read_and_verify(name);
    TEST_CHECK(res >= 0);
  }

  return TEST_RES_OK;
}
TEST_END


TEST(truncate_big_file)
{
  int size = (FS_PURE_DATA_PAGES(FS) / 2) * PSRAMFS_DATA_PAGE_SIZE(FS);
  printf("  filesize %i\n", size);
  int res = test_create_and_write_file("bigfile", size, size);
  TEST_CHECK(res >= 0);
  res = read_and_verify("bigfile");
  TEST_CHECK(res >= 0);
  psramfs_file fd = PSRAMFS_open(FS, "bigfile", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd > 0);
  res = PSRAMFS_fremove(FS, fd);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd);

  fd = PSRAMFS_open(FS, "bigfile", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_NOT_FOUND);

  return TEST_RES_OK;
}
TEST_END

TEST(ftruncate_file)
{
  int truncated_len = 0;
  char input[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  char output[sizeof(input)];

  psramfs_file fd = PSRAMFS_open(FS, "ftruncate", PSRAMFS_WRONLY | PSRAMFS_CREAT | PSRAMFS_TRUNC , 0);
  TEST_CHECK(fd > 0);
  TEST_CHECK_EQ(strlen(input), PSRAMFS_write(FS, fd, input, strlen(input)));

  // Extending file beyond size is not supported
  TEST_CHECK_EQ(PSRAMFS_ERR_END_OF_OBJECT, PSRAMFS_ftruncate(FS, fd, strlen(input) + 1));
  TEST_CHECK_EQ(PSRAMFS_ERR_END_OF_OBJECT, PSRAMFS_ftruncate(FS, fd, -1));

  // Truncating should succeed
  const char truncated_1[] = "ABCDEFGHIJ";
  truncated_len = strlen(truncated_1);
  TEST_CHECK_EQ(0, PSRAMFS_ftruncate(FS, fd, truncated_len));
  TEST_CHECK_EQ(0, PSRAMFS_close(FS, fd));

  // open file for reading and validate the content
  fd = PSRAMFS_open(FS, "ftruncate", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);
  memset(output, 0, sizeof(output));
  TEST_CHECK_EQ(truncated_len, PSRAMFS_read(FS, fd, output, sizeof(output)));
  TEST_CHECK_EQ(0, strncmp(truncated_1, output, truncated_len));
  TEST_CHECK_EQ(0, PSRAMFS_close(FS, fd));

  // further truncate the file
  fd = PSRAMFS_open(FS, "ftruncate", PSRAMFS_WRONLY, 0);
  TEST_CHECK(fd > 0);
  // Once truncated, the new file size should be the basis
  // whether truncation should succeed or not
  TEST_CHECK_EQ(PSRAMFS_ERR_END_OF_OBJECT, PSRAMFS_ftruncate(FS, fd, truncated_len + 1));
  TEST_CHECK_EQ(PSRAMFS_ERR_END_OF_OBJECT, PSRAMFS_ftruncate(FS, fd, strlen(input)));
  TEST_CHECK_EQ(PSRAMFS_ERR_END_OF_OBJECT, PSRAMFS_ftruncate(FS, fd, strlen(input) + 1));
  TEST_CHECK_EQ(PSRAMFS_ERR_END_OF_OBJECT, PSRAMFS_ftruncate(FS, fd, -1));

  // Truncating a truncated file should succeed
  const char truncated_2[] = "ABCDE";
  truncated_len = strlen(truncated_2);

  TEST_CHECK_EQ(0, PSRAMFS_ftruncate(FS, fd, truncated_len));
  TEST_CHECK_EQ(0, PSRAMFS_close(FS, fd));

  // open file for reading and validate the content
  fd = PSRAMFS_open(FS, "ftruncate", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);

  memset(output, 0, sizeof(output));

  TEST_CHECK_EQ(truncated_len, PSRAMFS_read(FS, fd, output, sizeof(output)));
  TEST_CHECK_EQ(0, strncmp(truncated_2, output, truncated_len));

  TEST_CHECK_EQ(0, PSRAMFS_close(FS, fd));

  return TEST_RES_OK;
}
TEST_END

TEST(simultaneous_write) {
  int res = PSRAMFS_creat(FS, "simul1", 0);
  TEST_CHECK(res >= 0);

  psramfs_file fd1 = PSRAMFS_open(FS, "simul1", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd1 > 0);
  psramfs_file fd2 = PSRAMFS_open(FS, "simul1", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd2 > 0);
  psramfs_file fd3 = PSRAMFS_open(FS, "simul1", PSRAMFS_RDWR, 0);
  TEST_CHECK(fd3 > 0);

  u8_t data1 = 1;
  u8_t data2 = 2;
  u8_t data3 = 3;

  res = PSRAMFS_write(FS, fd1, &data1, 1);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd1);
  res = PSRAMFS_write(FS, fd2, &data2, 1);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd2);
  res = PSRAMFS_write(FS, fd3, &data3, 1);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd3);

  psramfs_stat s;
  res = PSRAMFS_stat(FS, "simul1", &s);
  TEST_CHECK(res >= 0);

  TEST_CHECK(s.size == 1);

  u8_t rdata;
  psramfs_file fd = PSRAMFS_open(FS, "simul1", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);
  res = PSRAMFS_read(FS, fd, &rdata, 1);
  TEST_CHECK(res >= 0);

  TEST_CHECK(rdata == data3);

  return TEST_RES_OK;
}
TEST_END


TEST(simultaneous_write_append) {
  int res = PSRAMFS_creat(FS, "simul2", 0);
  TEST_CHECK(res >= 0);

  psramfs_file fd1 = PSRAMFS_open(FS, "simul2", PSRAMFS_RDWR | PSRAMFS_APPEND, 0);
  TEST_CHECK(fd1 > 0);
  psramfs_file fd2 = PSRAMFS_open(FS, "simul2", PSRAMFS_RDWR | PSRAMFS_APPEND, 0);
  TEST_CHECK(fd2 > 0);
  psramfs_file fd3 = PSRAMFS_open(FS, "simul2", PSRAMFS_RDWR | PSRAMFS_APPEND, 0);
  TEST_CHECK(fd3 > 0);

  u8_t data1 = 1;
  u8_t data2 = 2;
  u8_t data3 = 3;

  res = PSRAMFS_write(FS, fd1, &data1, 1);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd1);
  res = PSRAMFS_write(FS, fd2, &data2, 1);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd2);
  res = PSRAMFS_write(FS, fd3, &data3, 1);
  TEST_CHECK(res >= 0);
  PSRAMFS_close(FS, fd3);

  psramfs_stat s;
  res = PSRAMFS_stat(FS, "simul2", &s);
  TEST_CHECK(res >= 0);

  TEST_CHECK(s.size == 3);

  u8_t rdata[3];
  psramfs_file fd = PSRAMFS_open(FS, "simul2", PSRAMFS_RDONLY, 0);
  TEST_CHECK(fd > 0);
  res = PSRAMFS_read(FS, fd, &rdata, 3);
  TEST_CHECK(res >= 0);

  TEST_CHECK(rdata[0] == data1);
  TEST_CHECK(rdata[1] == data2);
  TEST_CHECK(rdata[2] == data3);

  return TEST_RES_OK;
}
TEST_END

TEST(file_uniqueness)
{
  int res;
  psramfs_file fd;
  char fname[32];
  int files = ((PSRAMFS_CFG_PHYS_SZ(FS) * 75) / 100) / 2 / PSRAMFS_CFG_LOG_PAGE_SZ(FS);
      //(FS_PURE_DATA_PAGES(FS) / 2) - PSRAMFS_PAGES_PER_BLOCK(FS)*8;
  int i;
  printf("  creating %i files\n", files);
  for (i = 0; i < files; i++) {
    char content[20];
    sprintf(fname, "file%i", i);
    sprintf(content, "%i", i);
    res = test_create_file(fname);
    TEST_CHECK(res >= 0);
    fd = PSRAMFS_open(FS, fname, PSRAMFS_APPEND | PSRAMFS_RDWR, 0);
    TEST_CHECK(fd >= 0);
    res = PSRAMFS_write(FS, fd, content, strlen(content)+1);
    TEST_CHECK(res >= 0);
    PSRAMFS_close(FS, fd);
  }
  printf("  checking %i files\n", files);
  for (i = 0; i < files; i++) {
    char content[20];
    char ref_content[20];
    sprintf(fname, "file%i", i);
    sprintf(content, "%i", i);
    fd = PSRAMFS_open(FS, fname, PSRAMFS_RDONLY, 0);
    TEST_CHECK(fd >= 0);
    res = PSRAMFS_read(FS, fd, ref_content, strlen(content)+1);
    TEST_CHECK(res >= 0);
    TEST_CHECK(strcmp(ref_content, content) == 0);
    PSRAMFS_close(FS, fd);
  }
  printf("  removing %i files\n", files/2);
  for (i = 0; i < files; i += 2) {
    sprintf(fname, "file%i", i);
    res = PSRAMFS_remove(FS, fname);
    TEST_CHECK(res >= 0);
  }
  printf("  creating %i files\n", files/2);
  for (i = 0; i < files; i += 2) {
    char content[20];
    sprintf(fname, "file%i", i);
    sprintf(content, "new%i", i);
    res = test_create_file(fname);
    TEST_CHECK(res >= 0);
    fd = PSRAMFS_open(FS, fname, PSRAMFS_APPEND | PSRAMFS_RDWR, 0);
    TEST_CHECK(fd >= 0);
    res = PSRAMFS_write(FS, fd, content, strlen(content)+1);
    TEST_CHECK(res >= 0);
    PSRAMFS_close(FS, fd);
  }
  printf("  checking %i files\n", files);
  for (i = 0; i < files; i++) {
    char content[20];
    char ref_content[20];
    sprintf(fname, "file%i", i);
    if ((i & 1) == 0) {
      sprintf(content, "new%i", i);
    } else {
      sprintf(content, "%i", i);
    }
    fd = PSRAMFS_open(FS, fname, PSRAMFS_RDONLY, 0);
    TEST_CHECK(fd >= 0);
    res = PSRAMFS_read(FS, fd, ref_content, strlen(content)+1);
    TEST_CHECK(res >= 0);
    TEST_CHECK(strcmp(ref_content, content) == 0);
    PSRAMFS_close(FS, fd);
  }

  return TEST_RES_OK;
}
TEST_END

int create_and_read_back(int size, int chunk) {
  char *name = "file";
  psramfs_file fd;
  s32_t res;

  u8_t *buf = malloc(size);
  memrand(buf, size);

  res = test_create_file(name);
  CHECK(res >= 0);
  fd = PSRAMFS_open(FS, name, PSRAMFS_APPEND | PSRAMFS_RDWR, 0);
  CHECK(fd >= 0);
  res = PSRAMFS_write(FS, fd, buf, size);
  CHECK(res >= 0);

  psramfs_stat stat;
  res = PSRAMFS_fstat(FS, fd, &stat);
  CHECK(res >= 0);
  CHECK(stat.size == size);

  PSRAMFS_close(FS, fd);

  fd = PSRAMFS_open(FS, name, PSRAMFS_RDONLY, 0);
  CHECK(fd >= 0);

  u8_t *rbuf = malloc(size);
  int offs = 0;
  while (offs < size) {
    int len = MIN(size - offs, chunk);
    res = PSRAMFS_read(FS, fd, &rbuf[offs], len);
    CHECK(res >= 0);
    CHECK(memcmp(&rbuf[offs], &buf[offs], len) == 0);

    offs += chunk;
  }

  CHECK(memcmp(&rbuf[0], &buf[0], size) == 0);

  PSRAMFS_close(FS, fd);

  free(rbuf);
  free(buf);

  return 0;
}

TEST(read_chunk_1)
{
  TEST_CHECK(create_and_read_back(PSRAMFS_DATA_PAGE_SIZE(FS)*8, 1) == 0);
  return TEST_RES_OK;
}
TEST_END


TEST(read_chunk_page)
{
  TEST_CHECK(create_and_read_back(PSRAMFS_DATA_PAGE_SIZE(FS)*(PSRAMFS_PAGES_PER_BLOCK(FS) - PSRAMFS_OBJ_LOOKUP_PAGES(FS))*2,
      PSRAMFS_DATA_PAGE_SIZE(FS)) == 0);
  return TEST_RES_OK;
}
TEST_END


TEST(read_chunk_index)
{
  TEST_CHECK(create_and_read_back(PSRAMFS_DATA_PAGE_SIZE(FS)*(PSRAMFS_PAGES_PER_BLOCK(FS) - PSRAMFS_OBJ_LOOKUP_PAGES(FS))*4,
      PSRAMFS_DATA_PAGE_SIZE(FS)*(PSRAMFS_PAGES_PER_BLOCK(FS) - PSRAMFS_OBJ_LOOKUP_PAGES(FS))) == 0);
  return TEST_RES_OK;
}
TEST_END


TEST(read_chunk_huge)
{
  int sz = (2*PSRAMFS_CFG_PHYS_SZ(FS))/3;
  TEST_CHECK(create_and_read_back(sz, sz) == 0);
  return TEST_RES_OK;
}
TEST_END


TEST(read_beyond)
{
  char *name = "file";
  psramfs_file fd;
  s32_t res;
  u32_t size = PSRAMFS_DATA_PAGE_SIZE(FS)*2;

  u8_t *buf = malloc(size);
  memrand(buf, size);

  res = test_create_file(name);
  CHECK(res >= 0);
  fd = PSRAMFS_open(FS, name, PSRAMFS_APPEND | PSRAMFS_RDWR, 0);
  CHECK(fd >= 0);
  res = PSRAMFS_write(FS, fd, buf, size);
  CHECK(res >= 0);

  psramfs_stat stat;
  res = PSRAMFS_fstat(FS, fd, &stat);
  CHECK(res >= 0);
  CHECK(stat.size == size);

  PSRAMFS_close(FS, fd);

  fd = PSRAMFS_open(FS, name, PSRAMFS_RDONLY, 0);
  CHECK(fd >= 0);

  u8_t *rbuf = malloc(size+10);
  res = PSRAMFS_read(FS, fd, rbuf, size+10);

  PSRAMFS_close(FS, fd);

  free(rbuf);
  free(buf);

  TEST_CHECK(res == size);

  return TEST_RES_OK;
}
TEST_END

TEST(read_beyond2)
{
  char *name = "file";
  psramfs_file fd;
  s32_t res;
  const s32_t size = PSRAMFS_DATA_PAGE_SIZE(FS);

  u8_t buf[size*2];
  memrand(buf, size);

  res = test_create_file(name);
  CHECK(res >= 0);
  fd = PSRAMFS_open(FS, name, PSRAMFS_APPEND | PSRAMFS_RDWR, 0);
  CHECK(fd >= 0);
  res = PSRAMFS_write(FS, fd, buf, size);
  CHECK(res >= 0);

  psramfs_stat stat;
  res = PSRAMFS_fstat(FS, fd, &stat);
  CHECK(res >= 0);
  CHECK(stat.size == size);

  PSRAMFS_close(FS, fd);

  int i,j;
  for (j = 1; j <= size+1; j++) {
    fd = PSRAMFS_open(FS, name, PSRAMFS_RDONLY, 0);
    CHECK(fd >= 0);
    PSRAMFS_clearerr(FS);
    for (i = 0; i < size * 2; i += j) {
      u8_t dst;
      res = PSRAMFS_read(FS, fd, buf, j);
      TEST_CHECK_EQ(PSRAMFS_errno(FS), i < size ? PSRAMFS_OK : PSRAMFS_ERR_END_OF_OBJECT);
      TEST_CHECK_EQ(res, MIN(j, MAX(0, size - (i + j) + j)));
    }
    PSRAMFS_close(FS, fd);
  }

  return TEST_RES_OK;
}
TEST_END


TEST(bad_index_1) {
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

  // set object index entry 2 to a bad page, free
  u32_t addr = PSRAMFS_PAGE_TO_PADDR(FS, pix) + sizeof(psramfs_page_object_ix_header) + 2 * sizeof(psramfs_page_ix);
  psramfs_page_ix bad_pix_ref = (psramfs_page_ix)-1;
  area_write(addr, (u8_t*)&bad_pix_ref, sizeof(psramfs_page_ix));

#if PSRAMFS_CACHE
  // delete all cache
  psramfs_cache *cache = psramfs_get_cache(FS);
  cache->cpage_use_map = 0;
#endif

  res = read_and_verify("file");
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_INDEX_REF_FREE);

  return TEST_RES_OK;
} TEST_END


TEST(bad_index_2) {
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

  // set object index entry 2 to a bad page, lu
  u32_t addr = PSRAMFS_PAGE_TO_PADDR(FS, pix) + sizeof(psramfs_page_object_ix_header) + 2 * sizeof(psramfs_page_ix);
  psramfs_page_ix bad_pix_ref = PSRAMFS_OBJ_LOOKUP_PAGES(FS)-1;
  area_write(addr, (u8_t*)&bad_pix_ref, sizeof(psramfs_page_ix));

#if PSRAMFS_CACHE
  // delete all cache
  psramfs_cache *cache = psramfs_get_cache(FS);
  cache->cpage_use_map = 0;
#endif

  res = read_and_verify("file");
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_INDEX_REF_LU);

  return TEST_RES_OK;
} TEST_END


TEST(lseek_simple_modification) {
  int res;
  psramfs_file fd;
  char *fname = "seekfile";
  int len = 4096;
  fd = PSRAMFS_open(FS, fname, PSRAMFS_TRUNC | PSRAMFS_CREAT | PSRAMFS_RDWR, 0);
  TEST_CHECK(fd > 0);
  int pfd = open(make_test_fname(fname), O_TRUNC | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  u8_t *buf = malloc(len);
  memrand(buf, len);
  res = PSRAMFS_write(FS, fd, buf, len);
  TEST_CHECK(res >= 0);
  write(pfd, buf, len);
  free(buf);
  res = read_and_verify(fname);
  TEST_CHECK(res >= 0);

  res = PSRAMFS_lseek(FS, fd, len/2, PSRAMFS_SEEK_SET);
  TEST_CHECK(res >= 0);
  lseek(pfd, len/2, SEEK_SET);
  len = len/4;
  buf = malloc(len);
  memrand(buf, len);
  res = PSRAMFS_write(FS, fd, buf, len);
  TEST_CHECK(res >= 0);
  write(pfd, buf, len);
  free(buf);

  res = read_and_verify(fname);
  TEST_CHECK(res >= 0);

  PSRAMFS_close(FS, fd);
  close(pfd);

  return TEST_RES_OK;
}
TEST_END


TEST(lseek_modification_append) {
  int res;
  psramfs_file fd;
  char *fname = "seekfile";
  int len = 4096;
  fd = PSRAMFS_open(FS, fname, PSRAMFS_TRUNC | PSRAMFS_CREAT | PSRAMFS_RDWR, 0);
  TEST_CHECK(fd > 0);
  int pfd = open(make_test_fname(fname), O_TRUNC | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  u8_t *buf = malloc(len);
  memrand(buf, len);
  res = PSRAMFS_write(FS, fd, buf, len);
  TEST_CHECK(res >= 0);
  write(pfd, buf, len);
  free(buf);
  res = read_and_verify(fname);
  TEST_CHECK(res >= 0);

  res = PSRAMFS_lseek(FS, fd, len/2, PSRAMFS_SEEK_SET);
  TEST_CHECK(res >= 0);
  lseek(pfd, len/2, SEEK_SET);

  buf = malloc(len);
  memrand(buf, len);
  res = PSRAMFS_write(FS, fd, buf, len);
  TEST_CHECK(res >= 0);
  write(pfd, buf, len);
  free(buf);

  res = read_and_verify(fname);
  TEST_CHECK(res >= 0);

  PSRAMFS_close(FS, fd);
  close(pfd);

  return TEST_RES_OK;
}
TEST_END


TEST(lseek_modification_append_multi) {
  int res;
  psramfs_file fd;
  char *fname = "seekfile";
  int len = 1024;
  int runs = (FS_PURE_DATA_PAGES(FS) / 2) * PSRAMFS_DATA_PAGE_SIZE(FS) / (len/2);

  fd = PSRAMFS_open(FS, fname, PSRAMFS_TRUNC | PSRAMFS_CREAT | PSRAMFS_RDWR, 0);
  TEST_CHECK(fd > 0);
  int pfd = open(make_test_fname(fname), O_TRUNC | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  u8_t *buf = malloc(len);
  memrand(buf, len);
  res = PSRAMFS_write(FS, fd, buf, len);
  TEST_CHECK(res >= 0);
  write(pfd, buf, len);
  free(buf);
  res = read_and_verify(fname);
  TEST_CHECK(res >= 0);

  while (runs--) {
    res = PSRAMFS_lseek(FS, fd, -len/2, PSRAMFS_SEEK_END);
    TEST_CHECK(res >= 0);
    lseek(pfd, -len/2, SEEK_END);

    buf = malloc(len);
    memrand(buf, len);
    res = PSRAMFS_write(FS, fd, buf, len);
    TEST_CHECK(res >= 0);
    write(pfd, buf, len);
    free(buf);

    res = read_and_verify(fname);
    TEST_CHECK(res >= 0);
  }

  PSRAMFS_close(FS, fd);
  close(pfd);

  return TEST_RES_OK;
}
TEST_END


TEST(lseek_read) {
  int res;
  psramfs_file fd;
  char *fname = "seekfile";
  int len = (FS_PURE_DATA_PAGES(FS) / 2) * PSRAMFS_DATA_PAGE_SIZE(FS);
  int runs = 100000;

  fd = PSRAMFS_open(FS, fname, PSRAMFS_TRUNC | PSRAMFS_CREAT | PSRAMFS_RDWR, 0);
  TEST_CHECK(fd > 0);
  u8_t *refbuf = malloc(len);
  memrand(refbuf, len);
  res = PSRAMFS_write(FS, fd, refbuf, len);
  TEST_CHECK(res >= 0);

  int offs = 0;
  res = PSRAMFS_lseek(FS, fd, 0, PSRAMFS_SEEK_SET);
  TEST_CHECK(res >= 0);

  while (runs--) {
    int i;
    u8_t buf[64];
    if (offs + 41 + sizeof(buf) >= len) {
      offs = (offs + 41 + sizeof(buf)) % len;
      res = PSRAMFS_lseek(FS, fd, offs, PSRAMFS_SEEK_SET);
      TEST_CHECK(res >= 0);
    }
    res = PSRAMFS_lseek(FS, fd, 41, PSRAMFS_SEEK_CUR);
    TEST_CHECK(res >= 0);
    offs += 41;
    res = PSRAMFS_read(FS, fd, buf, sizeof(buf));
    TEST_CHECK(res >= 0);
    for (i = 0; i < sizeof(buf); i++) {
      if (buf[i] != refbuf[offs+i]) {
        printf("  mismatch at offs %i\n", offs);
      }
      TEST_CHECK(buf[i] == refbuf[offs+i]);
    }
    offs += sizeof(buf);

    res = PSRAMFS_lseek(FS, fd, -((u32_t)sizeof(buf)+11), PSRAMFS_SEEK_CUR);
    TEST_CHECK(res >= 0);
    offs -= (sizeof(buf)+11);
    res = PSRAMFS_read(FS, fd, buf, sizeof(buf));
    TEST_CHECK(res >= 0);
    for (i = 0; i < sizeof(buf); i++) {
      if (buf[i] != refbuf[offs+i]) {
        printf("  mismatch at offs %i\n", offs);
      }
      TEST_CHECK(buf[i] == refbuf[offs+i]);
    }
    offs += sizeof(buf);
  }

  free(refbuf);
  PSRAMFS_close(FS, fd);

  return TEST_RES_OK;
}
TEST_END



TEST(lseek_oob) {
  int res;
  psramfs_file fd;
  char *fname = "seekfile";
  int len = (FS_PURE_DATA_PAGES(FS) / 2) * PSRAMFS_DATA_PAGE_SIZE(FS);

  fd = PSRAMFS_open(FS, fname, PSRAMFS_TRUNC | PSRAMFS_CREAT | PSRAMFS_RDWR, 0);
  TEST_CHECK(fd > 0);
  u8_t *refbuf = malloc(len);
  memrand(refbuf, len);
  res = PSRAMFS_write(FS, fd, refbuf, len);
  TEST_CHECK(res >= 0);

  int offs = 0;
  res = PSRAMFS_lseek(FS, fd, -1, PSRAMFS_SEEK_SET);
  TEST_CHECK_EQ(res, PSRAMFS_ERR_SEEK_BOUNDS);
  res = PSRAMFS_lseek(FS, fd, len+1, PSRAMFS_SEEK_SET);
  TEST_CHECK_EQ(res, PSRAMFS_ERR_END_OF_OBJECT);
  free(refbuf);
  PSRAMFS_close(FS, fd);

  return TEST_RES_OK;
}
TEST_END


TEST(gc_quick)
{
  char name[32];
  int f;
  int size = PSRAMFS_DATA_PAGE_SIZE(FS);
  int pages_per_block=PSRAMFS_PAGES_PER_BLOCK(FS) - PSRAMFS_OBJ_LOOKUP_PAGES(FS);
  int files = (pages_per_block+1)/2;
  int res;

  // negative, try quick gc on clean sys
  res = PSRAMFS_gc_quick(FS, 0);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_NO_DELETED_BLOCKS);

  // fill block with files
  for (f = 0; f < files; f++) {
    sprintf(name, "file%i", f);
    res = test_create_and_write_file(name, size, 1);
    TEST_CHECK(res >= 0);
  }
  for (f = 0; f < files; f++) {
    sprintf(name, "file%i", f);
    res = read_and_verify(name);
    TEST_CHECK(res >= 0);
  }
  // remove all files in block
  for (f = 0; f < files; f++) {
    sprintf(name, "file%i", f);
    res = PSRAMFS_remove(FS, name);
    TEST_CHECK(res >= 0);
  }

  // do a quick gc
  res = PSRAMFS_gc_quick(FS, 0);
  TEST_CHECK(res >= 0);

  // fill another block with files but two pages
  // We might have one deleted page left over from the previous gc, in case pages_per_block is odd.
  int pages_already=2*files-pages_per_block;
  int files2=(pages_per_block-pages_already+1)/2;

  for (f = 0; f < files2 - 1; f++) {
    sprintf(name, "file%i", f);
    res = test_create_and_write_file(name, size, 1);
    TEST_CHECK(res >= 0);
  }
  for (f = 0; f < files2 - 1; f++) {
    sprintf(name, "file%i", f);
    res = read_and_verify(name);
    TEST_CHECK(res >= 0);
  }
  // remove all files in block leaving two free pages in block
  for (f = 0; f < files2 - 1; f++) {
    sprintf(name, "file%i", f);
    res = PSRAMFS_remove(FS, name);
    TEST_CHECK(res >= 0);
  }

  // negative, try quick gc where no fully deleted blocks exist
  res = PSRAMFS_gc_quick(FS, 0);
  TEST_CHECK(res < 0);
  TEST_CHECK(PSRAMFS_errno(FS) == PSRAMFS_ERR_NO_DELETED_BLOCKS);

  // positive, try quick gc where allowing two free pages
  res = PSRAMFS_gc_quick(FS, 2);
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
}
TEST_END


TEST(write_small_file_chunks_1)
{
  int res = test_create_and_write_file("smallfile", 256, 1);
  TEST_CHECK(res >= 0);
  res = read_and_verify("smallfile");
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
}
TEST_END


TEST(write_small_files_chunks_1)
{
  char name[32];
  int f;
  int size = 512;
  int files = ((20*PSRAMFS_CFG_PHYS_SZ(FS))/100)/size;
  int res;
  for (f = 0; f < files; f++) {
    sprintf(name, "smallfile%i", f);
    res = test_create_and_write_file(name, size, 1);
    TEST_CHECK(res >= 0);
  }
  for (f = 0; f < files; f++) {
    sprintf(name, "smallfile%i", f);
    res = read_and_verify(name);
    TEST_CHECK(res >= 0);
  }

  return TEST_RES_OK;
}
TEST_END

TEST(write_big_file_chunks_1)
{
  int size = ((50*PSRAMFS_CFG_PHYS_SZ(FS))/100);
  printf("  filesize %i\n", size);
  int res = test_create_and_write_file("bigfile", size, 1);
  TEST_CHECK(res >= 0);
  res = read_and_verify("bigfile");
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
}
TEST_END

TEST(write_big_files_chunks_1)
{
  char name[32];
  int f;
  int files = 10;
  int res;
  int size = ((50*PSRAMFS_CFG_PHYS_SZ(FS))/100)/files;
  printf("  filesize %i\n", size);
  for (f = 0; f < files; f++) {
    sprintf(name, "bigfile%i", f);
    res = test_create_and_write_file(name, size, 1);
    TEST_CHECK(res >= 0);
  }
  for (f = 0; f < files; f++) {
    sprintf(name, "bigfile%i", f);
    res = read_and_verify(name);
    TEST_CHECK(res >= 0);
  }

  return TEST_RES_OK;
}
TEST_END


TEST(long_run_config_many_small_one_long)
{
  tfile_conf cfgs[] = {
      {   .tsize = LARGE,     .ttype = MODIFIED,      .tlife = LONG
      },
      {   .tsize = SMALL,     .ttype = UNTAMPERED,    .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = UNTAMPERED,    .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = LONG
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = LONG
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = LONG
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = LONG
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = LONG
      },
  };

  int res = run_file_config(sizeof(cfgs)/sizeof(cfgs[0]), &cfgs[0], 206, 5, 0);
  TEST_CHECK(res >= 0);
  return TEST_RES_OK;
}
TEST_END

TEST(long_run_config_many_medium)
{
  tfile_conf cfgs[] = {
      {   .tsize = MEDIUM,    .ttype = MODIFIED,      .tlife = LONG
      },
      {   .tsize = MEDIUM,    .ttype = APPENDED,      .tlife = LONG
      },
      {   .tsize = LARGE,     .ttype = MODIFIED,      .tlife = LONG
      },
      {   .tsize = MEDIUM,    .ttype = APPENDED,      .tlife = LONG
      },
      {   .tsize = MEDIUM,    .ttype = MODIFIED,      .tlife = LONG
      },
      {   .tsize = MEDIUM,    .ttype = MODIFIED,      .tlife = LONG
      },
      {   .tsize = MEDIUM,    .ttype = APPENDED,      .tlife = LONG
      },
      {   .tsize = LARGE,     .ttype = MODIFIED,      .tlife = LONG
      },
      {   .tsize = MEDIUM,    .ttype = APPENDED,      .tlife = LONG
      },
      {   .tsize = MEDIUM,    .ttype = MODIFIED,      .tlife = LONG
      },
      {   .tsize = MEDIUM,    .ttype = MODIFIED,      .tlife = LONG
      },
      {   .tsize = MEDIUM,    .ttype = APPENDED,      .tlife = LONG
      },
      {   .tsize = LARGE,     .ttype = MODIFIED,      .tlife = LONG
      },
      {   .tsize = MEDIUM,    .ttype = APPENDED,      .tlife = LONG
      },
      {   .tsize = MEDIUM,    .ttype = MODIFIED,      .tlife = LONG
      },
  };

  int res = run_file_config(sizeof(cfgs)/sizeof(cfgs[0]), &cfgs[0], 305, 5, 0);
  TEST_CHECK(res >= 0);
  return TEST_RES_OK;
}
TEST_END


TEST(long_run_config_many_small)
{
  tfile_conf cfgs[] = {
      {   .tsize = SMALL,     .ttype = APPENDED,      .tlife = LONG
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,      .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,      .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = UNTAMPERED,    .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = UNTAMPERED,    .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,    .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,    .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = APPENDED,      .tlife = LONG
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,      .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,      .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = UNTAMPERED,    .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = UNTAMPERED,    .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,    .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,    .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,      .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,      .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = UNTAMPERED,    .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = UNTAMPERED,    .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,    .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,    .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = MODIFIED,      .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,      .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,      .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = SHORT
      },
      {   .tsize = SMALL,     .ttype = UNTAMPERED,    .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = UNTAMPERED,    .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = SHORT
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,    .tlife = NORMAL
      },
      {   .tsize = EMPTY,     .ttype = UNTAMPERED,    .tlife = SHORT
      },
  };

  int res = run_file_config(sizeof(cfgs)/sizeof(cfgs[0]), &cfgs[0], 115, 6, 0);
  TEST_CHECK(res >= 0);
  return TEST_RES_OK;
}
TEST_END


TEST(long_run)
{
  tfile_conf cfgs[] = {
      {   .tsize = EMPTY,     .ttype = APPENDED,      .tlife = NORMAL
      },
      {   .tsize = SMALL,     .ttype = REWRITTEN,     .tlife = SHORT
      },
      {   .tsize = MEDIUM,    .ttype = MODIFIED,      .tlife = SHORT
      },
      {   .tsize = MEDIUM,    .ttype = APPENDED,      .tlife = SHORT
      },
  };

  int macro_runs = 500;
  printf("  ");
  u32_t clob_size = PSRAMFS_CFG_PHYS_SZ(FS)/4;
  int res = test_create_and_write_file("long_clobber", clob_size, clob_size);
  TEST_CHECK(res >= 0);

  res = read_and_verify("long_clobber");
  TEST_CHECK(res >= 0);

  while (macro_runs--) {
    //printf("  ---- run %i ----\n", macro_runs);
    if ((macro_runs % 20) == 0) {
      printf(".");
      fflush(stdout);
    }
    res = run_file_config(sizeof(cfgs)/sizeof(cfgs[0]), &cfgs[0], 20, 2, 0);
    TEST_CHECK(res >= 0);
  }
  printf("\n");

  res = read_and_verify("long_clobber");
  TEST_CHECK(res >= 0);

  res = PSRAMFS_check(FS);
  TEST_CHECK(res >= 0);

  return TEST_RES_OK;
}
TEST_END

#if PSRAMFS_IX_MAP
TEST(ix_map_basic)
{
  // create a scattered file
  s32_t res;
  psramfs_file fd1, fd2;
  fd1 = PSRAMFS_open(FS, "1", PSRAMFS_O_CREAT | PSRAMFS_O_WRONLY, 0);
  TEST_CHECK_GT(fd1, 0);
  fd2 = PSRAMFS_open(FS, "2", PSRAMFS_O_CREAT | PSRAMFS_O_WRONLY, 0);
  TEST_CHECK_GT(fd2, 0);

  u8_t buf[PSRAMFS_DATA_PAGE_SIZE(FS)];
  int i;
  for (i = 0; i < PSRAMFS_CFG_PHYS_SZ(FS) / 4 / PSRAMFS_DATA_PAGE_SIZE(FS); i++) {
    memrand(buf, sizeof(buf));
    res = PSRAMFS_write(FS, fd1, buf, sizeof(buf));
    TEST_CHECK_GE(res, PSRAMFS_OK);
    memrand(buf, sizeof(buf));
    res = PSRAMFS_write(FS, fd2, buf, sizeof(buf));
    TEST_CHECK_GE(res, PSRAMFS_OK);
  }
  res = PSRAMFS_close(FS, fd1);
  TEST_CHECK_GE(res, PSRAMFS_OK);
  res = PSRAMFS_close(FS, fd2);
  TEST_CHECK_GE(res, PSRAMFS_OK);

  res = PSRAMFS_remove(FS, "2");
  TEST_CHECK_GE(res, PSRAMFS_OK);

  psramfs_stat s;
  res = PSRAMFS_stat(FS, "1", &s);
  TEST_CHECK_GE(res, PSRAMFS_OK);
  u32_t size = s.size;

  printf("file created, size: %i..\n", size);

  fd1 = PSRAMFS_open(FS, "1", PSRAMFS_O_RDONLY, 0);
  TEST_CHECK_GT(fd1, 0);
  printf(".. corresponding pix entries: %i\n", PSRAMFS_bytes_to_ix_map_entries(FS, size));

  u8_t rd_buf[PSRAMFS_CFG_LOG_PAGE_SZ(FS)];

  fd1 = PSRAMFS_open(FS, "1", PSRAMFS_O_RDONLY, 0);
  TEST_CHECK_GT(fd1, 0);

  clear_flash_ops_log();

  printf("reading file without memory mapped index\n");
  while ((res = PSRAMFS_read(FS, fd1, rd_buf, sizeof(rd_buf))) == sizeof(rd_buf));
  TEST_CHECK_GT(res, PSRAMFS_OK);

  res = PSRAMFS_OK;

  u32_t reads_without_ixmap = get_flash_ops_log_read_bytes();
  dump_flash_access_stats();

  u32_t crc_non_map_ix = get_psramfs_file_crc_by_fd(fd1);

  TEST_CHECK_EQ(PSRAMFS_close(FS, fd1), PSRAMFS_OK);


  printf("reading file with memory mapped index\n");
  psramfs_ix_map map;
  psramfs_page_ix ixbuf[PSRAMFS_bytes_to_ix_map_entries(FS, size)];

  fd1 = PSRAMFS_open(FS, "1", PSRAMFS_O_RDONLY, 0);
  TEST_CHECK_GT(fd1, 0);

  // map index to memory
  res = PSRAMFS_ix_map(FS, fd1, &map, 0, size, ixbuf);
  TEST_CHECK_GE(res, PSRAMFS_OK);

  clear_flash_ops_log();

  while ((res = PSRAMFS_read(FS, fd1, rd_buf, sizeof(rd_buf))) == sizeof(rd_buf));
  TEST_CHECK_GT(res, PSRAMFS_OK);
  u32_t reads_with_ixmap_pass1 = get_flash_ops_log_read_bytes();

  dump_flash_access_stats();

  u32_t crc_map_ix_pass1 = get_psramfs_file_crc_by_fd(fd1);

  TEST_CHECK_LT(reads_with_ixmap_pass1, reads_without_ixmap);

  TEST_CHECK_EQ(crc_non_map_ix, crc_map_ix_pass1);

  psramfs_page_ix ref_ixbuf[PSRAMFS_bytes_to_ix_map_entries(FS, size)];
  memcpy(ref_ixbuf, ixbuf, sizeof(ixbuf));

  // force a gc by creating small files until full, reordering the index
  printf("forcing gc, error ERR_FULL %i expected\n", PSRAMFS_ERR_FULL);
  res = PSRAMFS_OK;
  u32_t ix = 10;
  while (res == PSRAMFS_OK) {
    char name[32];
    sprintf(name, "%i", ix);
    res = test_create_and_write_file(name, PSRAMFS_CFG_LOG_BLOCK_SZ(FS), PSRAMFS_CFG_LOG_BLOCK_SZ(FS));
    ix++;
  }

  TEST_CHECK_EQ(PSRAMFS_errno(FS), PSRAMFS_ERR_FULL);

  // make sure the map array was altered
  TEST_CHECK_NEQ(0, memcmp(ref_ixbuf, ixbuf, sizeof(ixbuf)));

  TEST_CHECK_GE(PSRAMFS_lseek(FS, fd1, 0, PSRAMFS_SEEK_SET), PSRAMFS_OK);

  clear_flash_ops_log();
  while ((res = PSRAMFS_read(FS, fd1, rd_buf, sizeof(rd_buf))) == sizeof(rd_buf));
  TEST_CHECK_GT(res, PSRAMFS_OK);
  u32_t reads_with_ixmap_pass2 = get_flash_ops_log_read_bytes();

  TEST_CHECK_EQ(reads_with_ixmap_pass1, reads_with_ixmap_pass2);

  u32_t crc_map_ix_pass2 = get_psramfs_file_crc_by_fd(fd1);

  TEST_CHECK_EQ(crc_map_ix_pass1, crc_map_ix_pass2);

  TEST_CHECK_EQ(PSRAMFS_close(FS, fd1), PSRAMFS_OK);

  return TEST_RES_OK;
}
TEST_END

TEST(ix_map_remap)
{
  // create a file, 10 data pages long
  s32_t res;
  psramfs_file fd1;
  fd1 = PSRAMFS_open(FS, "1", PSRAMFS_O_CREAT | PSRAMFS_O_WRONLY, 0);
  TEST_CHECK_GT(fd1, 0);

  const int size_pages = 10;

  u8_t buf[PSRAMFS_DATA_PAGE_SIZE(FS)];
  int i;
  for (i = 0; i < size_pages; i++) {
    memrand(buf, sizeof(buf));
    res = PSRAMFS_write(FS, fd1, buf, sizeof(buf));
    TEST_CHECK_GE(res, PSRAMFS_OK);
  }
  res = PSRAMFS_close(FS, fd1);
  TEST_CHECK_GE(res, PSRAMFS_OK);

  psramfs_stat s;
  res = PSRAMFS_stat(FS, "1", &s);
  TEST_CHECK_GE(res, PSRAMFS_OK);
  u32_t size = s.size;

  printf("file created, size: %i..\n", size);

  fd1 = PSRAMFS_open(FS, "1", PSRAMFS_O_RDONLY, 0);
  TEST_CHECK_GT(fd1, 0);
  printf(".. corresponding pix entries: %i\n", PSRAMFS_bytes_to_ix_map_entries(FS, size) + 1);
  TEST_CHECK_EQ(PSRAMFS_bytes_to_ix_map_entries(FS, size), size_pages + 1);

  // map index to memory
  // move around, check validity
  const int entries = PSRAMFS_bytes_to_ix_map_entries(FS, size/2);
  psramfs_ix_map map;
  // add one extra for stack safeguarding
  psramfs_page_ix ixbuf[entries+1];
  psramfs_page_ix ixbuf_ref[entries+1];
  const psramfs_page_ix canary = (psramfs_page_ix)0x87654321;
  memset(ixbuf, 0xee, sizeof(ixbuf));
  ixbuf[entries] = canary;

  res = PSRAMFS_ix_map(FS, fd1, &map, 0, size/2, ixbuf);
  TEST_CHECK_GE(res, PSRAMFS_OK);

  for (i = 0; i < entries; i++) {
    printf("%04x ", ixbuf[i]);
  }
  printf("\n");

  memcpy(ixbuf_ref, ixbuf, sizeof(psramfs_page_ix) * entries);

  TEST_CHECK_EQ(PSRAMFS_ix_remap(FS, fd1, 0), PSRAMFS_OK);
  TEST_CHECK_EQ(canary, ixbuf[entries]);
  for (i = 0; i < entries; i++) {
    printf("%04x ", ixbuf[i]);
  }
  printf("\n");
  TEST_CHECK_EQ(0, memcmp(ixbuf_ref, ixbuf, sizeof(psramfs_page_ix) * entries));

  TEST_CHECK_EQ(PSRAMFS_ix_remap(FS, fd1, PSRAMFS_DATA_PAGE_SIZE(FS)), PSRAMFS_OK);
  for (i = 0; i < entries; i++) {
    printf("%04x ", ixbuf[i]);
  }
  printf("\n");
  TEST_CHECK_EQ(canary, ixbuf[entries]);
  TEST_CHECK_EQ(0, memcmp(&ixbuf_ref[1], ixbuf, sizeof(psramfs_page_ix) * (entries-1)));


  TEST_CHECK_EQ(PSRAMFS_ix_remap(FS, fd1, 0), PSRAMFS_OK);
  for (i = 0; i < entries; i++) {
    printf("%04x ", ixbuf[i]);
  }
  printf("\n");
  TEST_CHECK_EQ(canary, ixbuf[entries]);
  TEST_CHECK_EQ(0, memcmp(ixbuf_ref, ixbuf, sizeof(psramfs_page_ix) * entries));

  TEST_CHECK_EQ(PSRAMFS_ix_remap(FS, fd1, size/2), PSRAMFS_OK);
  TEST_CHECK_EQ(canary, ixbuf[entries]);

  for (i = 0; i < entries; i++) {
    printf("%04x ", ixbuf_ref[i]);
  }
  printf("\n");

  for (i = 0; i < entries; i++) {
    printf("%04x ", ixbuf[i]);
  }
  printf("\n");

  int matches = 0;
  for (i = 0; i < entries; i++) {
    int j;
    for (j = 0; j < entries; j++) {
      if (ixbuf_ref[i] == ixbuf[i]) {
        matches++;
      }
    }
  }
  TEST_CHECK_LE(matches, 1);

  return TEST_RES_OK;
}
TEST_END

TEST(ix_map_partial)
{
  // create a file, 10 data pages long
  s32_t res;
  psramfs_file fd;
  fd = PSRAMFS_open(FS, "1", PSRAMFS_O_CREAT | PSRAMFS_O_WRONLY, 0);
  TEST_CHECK_GT(fd, 0);

  const int size_pages = 10;

  u8_t buf[PSRAMFS_DATA_PAGE_SIZE(FS)];
  int i;
  for (i = 0; i < size_pages; i++) {
    memrand(buf, sizeof(buf));
    res = PSRAMFS_write(FS, fd, buf, sizeof(buf));
    TEST_CHECK_GE(res, PSRAMFS_OK);
  }
  res = PSRAMFS_close(FS, fd);
  TEST_CHECK_GE(res, PSRAMFS_OK);

  psramfs_stat s;
  res = PSRAMFS_stat(FS, "1", &s);
  TEST_CHECK_GE(res, PSRAMFS_OK);
  u32_t size = s.size;

  printf("file created, size: %i..\n", size);

  const u32_t crc_unmapped = get_psramfs_file_crc("1");

  fd = PSRAMFS_open(FS, "1", PSRAMFS_O_RDONLY, 0);
  TEST_CHECK_GT(fd, 0);

  // map index to memory
  const int entries = PSRAMFS_bytes_to_ix_map_entries(FS, size/2);
  psramfs_ix_map map;
  psramfs_page_ix ixbuf[entries];

  printf("map 0-50%%\n");
  res = PSRAMFS_ix_map(FS, fd, &map, 0, size/2, ixbuf);
  TEST_CHECK_GE(res, PSRAMFS_OK);

  const u32_t crc_mapped_beginning = get_psramfs_file_crc_by_fd(fd);
  TEST_CHECK_EQ(crc_mapped_beginning, crc_unmapped);

  printf("map 25-75%%\n");
  res = PSRAMFS_ix_remap(FS, fd, size/4);
  TEST_CHECK_GE(res, PSRAMFS_OK);

  const u32_t crc_mapped_middle = get_psramfs_file_crc_by_fd(fd);
  TEST_CHECK_EQ(crc_mapped_middle, crc_unmapped);

  printf("map 50-100%%\n");
  res = PSRAMFS_ix_remap(FS, fd, size/2);
  TEST_CHECK_GE(res, PSRAMFS_OK);

  const u32_t crc_mapped_end = get_psramfs_file_crc_by_fd(fd);
  TEST_CHECK_EQ(crc_mapped_end, crc_unmapped);

  return TEST_RES_OK;
}
TEST_END

TEST(ix_map_beyond)
{
  // create a file, 10 data pages long
  s32_t res;
  psramfs_file fd;
  fd = PSRAMFS_open(FS, "1", PSRAMFS_O_CREAT | PSRAMFS_O_WRONLY, 0);
  TEST_CHECK_GT(fd, 0);

  const int size_pages = 10;

  u8_t buf[PSRAMFS_DATA_PAGE_SIZE(FS)];
  int i;
  for (i = 0; i < size_pages; i++) {
    memrand(buf, sizeof(buf));
    res = PSRAMFS_write(FS, fd, buf, sizeof(buf));
    TEST_CHECK_GE(res, PSRAMFS_OK);
  }
  res = PSRAMFS_close(FS, fd);
  TEST_CHECK_GE(res, PSRAMFS_OK);

  psramfs_stat s;
  res = PSRAMFS_stat(FS, "1", &s);
  TEST_CHECK_GE(res, PSRAMFS_OK);
  u32_t size = s.size;

  printf("file created, size: %i..\n", size);

  // map index to memory
  fd = PSRAMFS_open(FS, "1", PSRAMFS_O_RDWR | PSRAMFS_O_APPEND, 0);
  TEST_CHECK_GT(fd, 0);

  const int entries = PSRAMFS_bytes_to_ix_map_entries(FS, size);
  psramfs_ix_map map;
  psramfs_page_ix ixbuf[entries];
  printf("map has %i entries\n", entries);

  printf("map 100-200%%\n");
  res = PSRAMFS_ix_map(FS, fd, &map, size, size, ixbuf);
  TEST_CHECK_GE(res, PSRAMFS_OK);

  printf("make sure map is empty\n");
  for (i = 0; i < entries; i++) {
    printf("%04x ", ixbuf[i]);
    TEST_CHECK_EQ(ixbuf[i], 0);
  }
  printf("\n");

  printf("elongate by 100%%\n");
  for (i = 0; i < size_pages; i++) {
    memrand(buf, sizeof(buf));
    res = PSRAMFS_write(FS, fd, buf, sizeof(buf));
    TEST_CHECK_GE(res, PSRAMFS_OK);
  }
  TEST_CHECK_GE(PSRAMFS_fflush(FS, fd), PSRAMFS_OK);

  res = PSRAMFS_stat(FS, "1", &s);
  TEST_CHECK_GE(res, PSRAMFS_OK);
  size = s.size;
  printf("file elongated, size: %i..\n", size);

  printf("make sure map is full but for one element\n");
  int zeroed = 0;
  for (i = 0; i < entries; i++) {
    printf("%04x ", ixbuf[i]);
    if (ixbuf[i] == 0) zeroed++;
  }
  printf("\n");
  TEST_CHECK_LE(zeroed, 1);

  printf("remap till end\n");
  TEST_CHECK_EQ(PSRAMFS_ix_remap(FS, fd, size), PSRAMFS_OK);

  printf("make sure map is empty but for one element\n");
  int nonzero = 0;
  for (i = 0; i < entries; i++) {
    printf("%04x ", ixbuf[i]);
    if (ixbuf[i]) nonzero++;
  }
  printf("\n");
  TEST_CHECK_LE(nonzero, 1);

  printf("elongate again, by other fd\n");

  psramfs_file fd2 = PSRAMFS_open(FS, "1", PSRAMFS_O_WRONLY | PSRAMFS_O_APPEND, 0);
  TEST_CHECK_GT(fd2, 0);

  for (i = 0; i < size_pages; i++) {
    memrand(buf, sizeof(buf));
    res = PSRAMFS_write(FS, fd2, buf, sizeof(buf));
    TEST_CHECK_GE(res, PSRAMFS_OK);
  }
  TEST_CHECK_GE(PSRAMFS_close(FS, fd2), PSRAMFS_OK);

  printf("make sure map is full but for one element\n");
  zeroed = 0;
  for (i = 0; i < entries; i++) {
    printf("%04x ", ixbuf[i]);
    if (ixbuf[i] == 0) zeroed++;
  }
  printf("\n");
  TEST_CHECK_LE(zeroed, 1);

  return TEST_RES_OK;
}
TEST_END

#endif // PSRAMFS_IX_MAP

SUITE_TESTS(hydrogen_tests)
  ADD_TEST(info)
#if PSRAMFS_USE_MAGIC
  ADD_TEST(magic)
#if PSRAMFS_USE_MAGIC_LENGTH
  ADD_TEST(magic_length)
#if PSRAMFS_SINGLETON==0
  ADD_TEST(magic_length_probe)
#endif
#endif
#endif
  ADD_TEST(missing_file)
  ADD_TEST(bad_fd)
  ADD_TEST(closed_fd)
  ADD_TEST(deleted_same_fd)
  ADD_TEST(deleted_other_fd)
  ADD_TEST(file_by_open)
  ADD_TEST(file_by_creat)
  ADD_TEST(file_by_open_excl)
#if PSRAMFS_FILEHDL_OFFSET
  ADD_TEST(open_fh_offs)
#endif
  ADD_TEST(list_dir)
  ADD_TEST(open_by_dirent)
  ADD_TEST(open_by_page)
  ADD_TEST(user_callback_basic)
  ADD_TEST(user_callback_gc)
  ADD_TEST(name_too_long)
  ADD_TEST(rename)
#if PSRAMFS_OBJ_META_LEN
  ADD_TEST(update_meta)
#endif
  ADD_TEST(remove_single_by_path)
  ADD_TEST(remove_single_by_fd)
  ADD_TEST(write_cache)
  ADD_TEST(write_big_file_chunks_page)
  ADD_TEST(write_big_files_chunks_page)
  ADD_TEST(write_big_file_chunks_index)
  ADD_TEST(write_big_files_chunks_index)
  ADD_TEST(write_big_file_chunks_huge)
  ADD_TEST(write_big_files_chunks_huge)
  ADD_TEST(truncate_big_file)
  ADD_TEST(ftruncate_file)
  ADD_TEST(simultaneous_write)
  ADD_TEST(simultaneous_write_append)
  ADD_TEST(file_uniqueness)
  ADD_TEST(read_chunk_1)
  ADD_TEST(read_chunk_page)
  ADD_TEST(read_chunk_index)
  ADD_TEST(read_chunk_huge)
  ADD_TEST(read_beyond)
  ADD_TEST(read_beyond2)
  ADD_TEST(bad_index_1)
  ADD_TEST(bad_index_2)
  ADD_TEST(lseek_simple_modification)
  ADD_TEST(lseek_modification_append)
  ADD_TEST(lseek_modification_append_multi)
  ADD_TEST(lseek_read)
  ADD_TEST(lseek_oob)
  ADD_TEST(gc_quick)
  ADD_TEST(write_small_file_chunks_1)
  ADD_TEST(write_small_files_chunks_1)
  ADD_TEST(write_big_file_chunks_1)
  ADD_TEST(write_big_files_chunks_1)
  ADD_TEST(long_run_config_many_small_one_long)
  ADD_TEST(long_run_config_many_medium)
  ADD_TEST(long_run_config_many_small)
  ADD_TEST(long_run)
#if PSRAMFS_IX_MAP
  ADD_TEST(ix_map_basic)
  ADD_TEST(ix_map_remap)
  ADD_TEST(ix_map_partial)
  ADD_TEST(ix_map_beyond)
#endif

SUITE_END(hydrogen_tests)

