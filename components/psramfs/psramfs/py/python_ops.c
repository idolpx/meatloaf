#include <stdlib.h>
#include <stdio.h>

#include "psramfs.h"

void *my_psramfs_mount(int phys_size,
                      int phys_addr,
                      int phys_erase_block,
                      int log_page_size,
                      int log_block_size,
                      s32_t (read_cb)(u32_t addr, u32_t size, u8_t *dst),
                      s32_t (write_cb)(u32_t addr, u32_t size, u8_t *src),
                      s32_t (erase_cb)(u32_t addr, u32_t size)
                      )
{

#define WORK_BUF_SIZE (log_page_size*2)
#define PSRAMFS_FDS_SIZE (32*4)
#define PSRAMFS_CACHE_BUF_SIZE (phys_size) //(log_page_size+32)*128)

	struct alloced {
		psram fs;
		uint8_t psramfs_work_buf[WORK_BUF_SIZE];
		uint8_t psramfs_fds[PSRAMFS_FDS_SIZE];
		uint8_t psramfs_cache_buf[PSRAMFS_CACHE_BUF_SIZE];
	};

	struct alloced *d = malloc(sizeof(struct alloced));
	psram *pfs = &d->fs;

	psramfs_config cfg;
	cfg.phys_size = phys_size; // use all spi flash
	cfg.phys_addr = phys_addr; // start psram at start of spi flash
	cfg.phys_erase_block = phys_erase_block; // according to datasheet
	cfg.log_block_size = log_block_size; // let us not complicate things
	cfg.log_page_size = log_page_size; // as we said

	cfg.hal_read_f = read_cb;
	cfg.hal_write_f = write_cb;
	cfg.hal_erase_f = erase_cb;

	int res = PSRAMFS_mount(pfs,
	                       &cfg,
	                       d->psramfs_work_buf,
	                       d->psramfs_fds,
	                       PSRAMFS_FDS_SIZE,
	                       d->psramfs_cache_buf,
	                       PSRAMFS_CACHE_BUF_SIZE,
	                       0);

	return res?NULL:(void *)pfs;
}

int my_psramfs_umount(psram *fs)
{
	PSRAMFS_clearerr(fs);
	PSRAMFS_unmount(fs);

	int ret = PSRAMFS_errno(fs);
	free(fs);

	return ret;
}

int my_dir(psram *fs,
           void (entry_cb)(char *name, int size, int id))
{

	psramfs_DIR d;
	struct psramfs_dirent e;
	struct psramfs_dirent *pe = &e;

	int ret;

	PSRAMFS_clearerr(fs);

	if (NULL==PSRAMFS_opendir(fs, "/", &d)) goto done;

	while ((pe = PSRAMFS_readdir(&d, pe))) {
		entry_cb(pe->name, pe->size, pe->obj_id);
	}

	if (PSRAMFS_closedir(&d)) goto done;
	PSRAMFS_clearerr(fs);

 done:
	return PSRAMFS_errno(fs);
}
