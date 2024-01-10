/*
 * psramfs_hydrogen.c
 *
 *  Created on: Jun 16, 2013
 *      Author: petera
 */

#include "psramfs.h"
#include "psramfs_nucleus.h"

#if PSRAMFS_CACHE == 1
static s32_t psramfs_fflush_cache(psram *fs, psramfs_file fh);
#endif

#if PSRAMFS_BUFFER_HELP
u32_t PSRAMFS_buffer_bytes_for_filedescs(psram *fs, u32_t num_descs) {
  return num_descs * sizeof(psramfs_fd);
}
#if PSRAMFS_CACHE
u32_t PSRAMFS_buffer_bytes_for_cache(psram *fs, u32_t num_pages) {
  return sizeof(psramfs_cache) + num_pages * (sizeof(psramfs_cache_page) + PSRAMFS_CFG_LOG_PAGE_SZ(fs));
}
#endif
#endif

u8_t PSRAMFS_mounted(psram *fs) {
  return PSRAMFS_CHECK_MOUNT(fs);
}

s32_t PSRAMFS_format(psram *fs) {
#if PSRAMFS_READ_ONLY
  (void)fs;
  return PSRAMFS_ERR_RO_NOT_IMPL;
#else
  PSRAMFS_API_CHECK_CFG(fs);
  if (PSRAMFS_CHECK_MOUNT(fs)) {
    fs->err_code = PSRAMFS_ERR_MOUNTED;
    return -1;
  }

  s32_t res;
  PSRAMFS_LOCK(fs);

  psramfs_block_ix bix = 0;
  while (bix < fs->block_count) {
    fs->max_erase_count = 0;
    res = psramfs_erase_block(fs, bix);
    if (res != PSRAMFS_OK) {
      res = PSRAMFS_ERR_ERASE_FAIL;
    }
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
    bix++;
  }

  PSRAMFS_UNLOCK(fs);

  return 0;
#endif // PSRAMFS_READ_ONLY
}

#if PSRAMFS_USE_MAGIC && PSRAMFS_USE_MAGIC_LENGTH && PSRAMFS_SINGLETON==0

s32_t PSRAMFS_probe_fs(psramfs_config *config) {
  PSRAMFS_API_DBG("%s\n", __func__);
  s32_t res = psramfs_probe(config);
  return res;
}

#endif // PSRAMFS_USE_MAGIC && PSRAMFS_USE_MAGIC_LENGTH && PSRAMFS_SINGLETON==0

s32_t PSRAMFS_mount(psram *fs, psramfs_config *config, u8_t *work,
    u8_t *fd_space, u32_t fd_space_size,
    void *cache, u32_t cache_size,
    psramfs_check_callback check_cb_f) {
  PSRAMFS_API_DBG("%s "
                 " sz:"_SPIPRIi " logpgsz:"_SPIPRIi " logblksz:"_SPIPRIi " perasz:"_SPIPRIi
                 " addr:"_SPIPRIad
                 " fdsz:"_SPIPRIi " cachesz:"_SPIPRIi
                 "\n",
                 __func__,
                 PSRAMFS_CFG_PHYS_SZ(fs),
                 PSRAMFS_CFG_LOG_PAGE_SZ(fs),
                 PSRAMFS_CFG_LOG_BLOCK_SZ(fs),
                 PSRAMFS_CFG_PHYS_ERASE_SZ(fs),
                 PSRAMFS_CFG_PHYS_ADDR(fs),
                 fd_space_size, cache_size);
  void *user_data;
  PSRAMFS_LOCK(fs);
  user_data = fs->user_data;
  memset(fs, 0, sizeof(psram));
  _PSRAMFS_MEMCPY(&fs->cfg, config, sizeof(psramfs_config));
  fs->user_data = user_data;
  fs->block_count = PSRAMFS_CFG_PHYS_SZ(fs) / PSRAMFS_CFG_LOG_BLOCK_SZ(fs);
  fs->work = &work[0];
  fs->lu_work = &work[PSRAMFS_CFG_LOG_PAGE_SZ(fs)];
  memset(fd_space, 0, fd_space_size);
  // align fd_space pointer to pointer size byte boundary
  u8_t ptr_size = sizeof(void*);
  u8_t addr_lsb = ((u8_t)(intptr_t)fd_space) & (ptr_size-1);
  if (addr_lsb) {
    fd_space += (ptr_size-addr_lsb);
    fd_space_size -= (ptr_size-addr_lsb);
  }
  fs->fd_space = fd_space;
  fs->fd_count = (fd_space_size/sizeof(psramfs_fd));

  // align cache pointer to 4 byte boundary
  addr_lsb = ((u8_t)(intptr_t)cache) & (ptr_size-1);
  if (addr_lsb) {
    u8_t *cache_8 = (u8_t *)cache;
    cache_8 += (ptr_size-addr_lsb);
    cache = cache_8;
    cache_size -= (ptr_size-addr_lsb);
  }
  if (cache_size & (ptr_size-1)) {
    cache_size -= (cache_size & (ptr_size-1));
  }

#if PSRAMFS_CACHE
  fs->cache = cache;
  fs->cache_size = (cache_size > (PSRAMFS_CFG_LOG_PAGE_SZ(fs)*32)) ? PSRAMFS_CFG_LOG_PAGE_SZ(fs)*32 : cache_size;
  psramfs_cache_init(fs);
#endif

  s32_t res;

#if PSRAMFS_USE_MAGIC
  res = PSRAMFS_CHECK_MAGIC_POSSIBLE(fs) ? PSRAMFS_OK : PSRAMFS_ERR_MAGIC_NOT_POSSIBLE;
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
#endif

  fs->config_magic = PSRAMFS_CONFIG_MAGIC;

  res = psramfs_obj_lu_scan(fs);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  PSRAMFS_DBG("page index byte len:         "_SPIPRIi"\n", (u32_t)PSRAMFS_CFG_LOG_PAGE_SZ(fs));
  PSRAMFS_DBG("object lookup pages:         "_SPIPRIi"\n", (u32_t)PSRAMFS_OBJ_LOOKUP_PAGES(fs));
  PSRAMFS_DBG("page pages per block:        "_SPIPRIi"\n", (u32_t)PSRAMFS_PAGES_PER_BLOCK(fs));
  PSRAMFS_DBG("page header length:          "_SPIPRIi"\n", (u32_t)sizeof(psramfs_page_header));
  PSRAMFS_DBG("object header index entries: "_SPIPRIi"\n", (u32_t)PSRAMFS_OBJ_HDR_IX_LEN(fs));
  PSRAMFS_DBG("object index entries:        "_SPIPRIi"\n", (u32_t)PSRAMFS_OBJ_IX_LEN(fs));
  PSRAMFS_DBG("available file descriptors:  "_SPIPRIi"\n", (u32_t)fs->fd_count);
  PSRAMFS_DBG("free blocks:                 "_SPIPRIi"\n", (u32_t)fs->free_blocks);

  fs->check_cb_f = check_cb_f;

  fs->mounted = 1;

  PSRAMFS_UNLOCK(fs);

  return 0;
}

void PSRAMFS_unmount(psram *fs) {
  PSRAMFS_API_DBG("%s\n", __func__);
  if (!PSRAMFS_CHECK_CFG(fs) || !PSRAMFS_CHECK_MOUNT(fs)) return;
  PSRAMFS_LOCK(fs);
  u32_t i;
  psramfs_fd *fds = (psramfs_fd *)fs->fd_space;
  for (i = 0; i < fs->fd_count; i++) {
    psramfs_fd *cur_fd = &fds[i];
    if (cur_fd->file_nbr != 0) {
#if PSRAMFS_CACHE
      (void)psramfs_fflush_cache(fs, cur_fd->file_nbr);
#endif
      psramfs_fd_return(fs, cur_fd->file_nbr);
    }
  }
  fs->mounted = 0;

  PSRAMFS_UNLOCK(fs);
}

s32_t PSRAMFS_errno(psram *fs) {
  return fs->err_code;
}

void PSRAMFS_clearerr(psram *fs) {
  PSRAMFS_API_DBG("%s\n", __func__);
  fs->err_code = PSRAMFS_OK;
}

s32_t PSRAMFS_creat(psram *fs, const char *path, psramfs_mode mode) {
  PSRAMFS_API_DBG("%s '%s'\n", __func__, path);
#if PSRAMFS_READ_ONLY
  (void)fs; (void)path; (void)mode;
  return PSRAMFS_ERR_RO_NOT_IMPL;
#else
  (void)mode;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  if (strlen(path) > PSRAMFS_OBJ_NAME_LEN - 1) {
    PSRAMFS_API_CHECK_RES(fs, PSRAMFS_ERR_NAME_TOO_LONG);
  }
  PSRAMFS_LOCK(fs);
  psramfs_obj_id obj_id;
  s32_t res;

  res = psramfs_obj_lu_find_free_obj_id(fs, &obj_id, (const u8_t*)path);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  res = psramfs_object_create(fs, obj_id, (const u8_t*)path, 0, PSRAMFS_TYPE_FILE, 0);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  PSRAMFS_UNLOCK(fs);
  return 0;
#endif // PSRAMFS_READ_ONLY
}

psramfs_file PSRAMFS_open(psram *fs, const char *path, psramfs_flags flags, psramfs_mode mode) {
  PSRAMFS_API_DBG("%s '%s' "_SPIPRIfl "\n", __func__, path, flags);
  (void)mode;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  if (strlen(path) > PSRAMFS_OBJ_NAME_LEN - 1) {
    PSRAMFS_API_CHECK_RES(fs, PSRAMFS_ERR_NAME_TOO_LONG);
  }
  PSRAMFS_LOCK(fs);

  psramfs_fd *fd;
  psramfs_page_ix pix;

#if PSRAMFS_READ_ONLY
  // not valid flags in read only mode
  flags &= ~(PSRAMFS_WRONLY | PSRAMFS_CREAT | PSRAMFS_TRUNC);
#endif // PSRAMFS_READ_ONLY

  s32_t res = psramfs_fd_find_new(fs, &fd, path);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_object_find_object_index_header_by_name(fs, (const u8_t*)path, &pix);
  if ((flags & PSRAMFS_O_CREAT) == 0) {
    if (res < PSRAMFS_OK) {
      psramfs_fd_return(fs, fd->file_nbr);
    }
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }

  if (res == PSRAMFS_OK &&
      (flags & (PSRAMFS_O_CREAT | PSRAMFS_O_EXCL)) == (PSRAMFS_O_CREAT | PSRAMFS_O_EXCL)) {
    // creat and excl and file exists - fail
    res = PSRAMFS_ERR_FILE_EXISTS;
    psramfs_fd_return(fs, fd->file_nbr);
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }

  if ((flags & PSRAMFS_O_CREAT) && res == PSRAMFS_ERR_NOT_FOUND) {
#if !PSRAMFS_READ_ONLY
    psramfs_obj_id obj_id;
    // no need to enter conflicting name here, already looked for it above
    res = psramfs_obj_lu_find_free_obj_id(fs, &obj_id, 0);
    if (res < PSRAMFS_OK) {
      psramfs_fd_return(fs, fd->file_nbr);
    }
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
    res = psramfs_object_create(fs, obj_id, (const u8_t*)path, 0, PSRAMFS_TYPE_FILE, &pix);
    if (res < PSRAMFS_OK) {
      psramfs_fd_return(fs, fd->file_nbr);
    }
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
    flags &= ~PSRAMFS_O_TRUNC;
#endif // !PSRAMFS_READ_ONLY
  } else {
    if (res < PSRAMFS_OK) {
      psramfs_fd_return(fs, fd->file_nbr);
    }
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }
  res = psramfs_object_open_by_page(fs, pix, fd, flags, mode);
  if (res < PSRAMFS_OK) {
    psramfs_fd_return(fs, fd->file_nbr);
  }
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
#if !PSRAMFS_READ_ONLY
  if (flags & PSRAMFS_O_TRUNC) {
    res = psramfs_object_truncate(fd, 0, 0);
    if (res < PSRAMFS_OK) {
      psramfs_fd_return(fs, fd->file_nbr);
    }
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }
#endif // !PSRAMFS_READ_ONLY

  fd->fdoffset = 0;

  PSRAMFS_UNLOCK(fs);

  return PSRAMFS_FH_OFFS(fs, fd->file_nbr);
}

psramfs_file PSRAMFS_open_by_dirent(psram *fs, struct psramfs_dirent *e, psramfs_flags flags, psramfs_mode mode) {
  PSRAMFS_API_DBG("%s '%s':"_SPIPRIid " "_SPIPRIfl "\n", __func__, e->name, e->obj_id, flags);
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  psramfs_fd *fd;

  s32_t res = psramfs_fd_find_new(fs, &fd, 0);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_object_open_by_page(fs, e->pix, fd, flags, mode);
  if (res < PSRAMFS_OK) {
    psramfs_fd_return(fs, fd->file_nbr);
  }
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
#if !PSRAMFS_READ_ONLY
  if (flags & PSRAMFS_O_TRUNC) {
    res = psramfs_object_truncate(fd, 0, 0);
    if (res < PSRAMFS_OK) {
      psramfs_fd_return(fs, fd->file_nbr);
    }
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }
#endif // !PSRAMFS_READ_ONLY

  fd->fdoffset = 0;

  PSRAMFS_UNLOCK(fs);

  return PSRAMFS_FH_OFFS(fs, fd->file_nbr);
}

psramfs_file PSRAMFS_open_by_page(psram *fs, psramfs_page_ix page_ix, psramfs_flags flags, psramfs_mode mode) {
  PSRAMFS_API_DBG("%s "_SPIPRIpg " "_SPIPRIfl "\n", __func__, page_ix, flags);
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  psramfs_fd *fd;

  s32_t res = psramfs_fd_find_new(fs, &fd, 0);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  if (PSRAMFS_IS_LOOKUP_PAGE(fs, page_ix)) {
    res = PSRAMFS_ERR_NOT_A_FILE;
    psramfs_fd_return(fs, fd->file_nbr);
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }

  res = psramfs_object_open_by_page(fs, page_ix, fd, flags, mode);
  if (res == PSRAMFS_ERR_IS_FREE ||
      res == PSRAMFS_ERR_DELETED ||
      res == PSRAMFS_ERR_NOT_FINALIZED ||
      res == PSRAMFS_ERR_NOT_INDEX ||
      res == PSRAMFS_ERR_INDEX_SPAN_MISMATCH) {
    res = PSRAMFS_ERR_NOT_A_FILE;
  }
  if (res < PSRAMFS_OK) {
    psramfs_fd_return(fs, fd->file_nbr);
  }
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

#if !PSRAMFS_READ_ONLY
  if (flags & PSRAMFS_O_TRUNC) {
    res = psramfs_object_truncate(fd, 0, 0);
    if (res < PSRAMFS_OK) {
      psramfs_fd_return(fs, fd->file_nbr);
    }
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }
#endif // !PSRAMFS_READ_ONLY

  fd->fdoffset = 0;

  PSRAMFS_UNLOCK(fs);

  return PSRAMFS_FH_OFFS(fs, fd->file_nbr);
}

static s32_t psramfs_hydro_read(psram *fs, psramfs_file fh, void *buf, s32_t len) {
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  psramfs_fd *fd;
  s32_t res;

  fh = PSRAMFS_FH_UNOFFS(fs, fh);
  res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  if ((fd->flags & PSRAMFS_O_RDONLY) == 0) {
    res = PSRAMFS_ERR_NOT_READABLE;
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }

  if (fd->size == PSRAMFS_UNDEFINED_LEN && len > 0) {
    // special case for zero sized files
    res = PSRAMFS_ERR_END_OF_OBJECT;
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }

#if PSRAMFS_CACHE_WR
  psramfs_fflush_cache(fs, fh);
#endif

  if (fd->fdoffset + len >= fd->size) {
    // reading beyond file size
    s32_t avail = fd->size - fd->fdoffset;
    if (avail <= 0) {
      PSRAMFS_API_CHECK_RES_UNLOCK(fs, PSRAMFS_ERR_END_OF_OBJECT);
    }
    res = psramfs_object_read(fd, fd->fdoffset, avail, (u8_t*)buf);
    if (res == PSRAMFS_ERR_END_OF_OBJECT) {
      fd->fdoffset += avail;
      PSRAMFS_UNLOCK(fs);
      return avail;
    } else {
      PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
      len = avail;
    }
  } else {
    // reading within file size
    res = psramfs_object_read(fd, fd->fdoffset, len, (u8_t*)buf);
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }
  fd->fdoffset += len;

  PSRAMFS_UNLOCK(fs);

  return len;
}

s32_t PSRAMFS_read(psram *fs, psramfs_file fh, void *buf, s32_t len) {
  PSRAMFS_API_DBG("%s "_SPIPRIfd " "_SPIPRIi "\n", __func__, fh, len);
  s32_t res = psramfs_hydro_read(fs, fh, buf, len);
  if (res == PSRAMFS_ERR_END_OF_OBJECT) {
    res = 0;
  }
  return res;
}


#if !PSRAMFS_READ_ONLY
static s32_t psramfs_hydro_write(psram *fs, psramfs_fd *fd, void *buf, u32_t offset, s32_t len) {
  (void)fs;
  s32_t res = PSRAMFS_OK;
  s32_t remaining = len;
  if (fd->size != PSRAMFS_UNDEFINED_LEN && offset < fd->size) {
    s32_t m_len = MIN((s32_t)(fd->size - offset), len);
    res = psramfs_object_modify(fd, offset, (u8_t *)buf, m_len);
    PSRAMFS_CHECK_RES(res);
    remaining -= m_len;
    u8_t *buf_8 = (u8_t *)buf;
    buf_8 += m_len;
    buf = buf_8;
    offset += m_len;
  }
  if (remaining > 0) {
    res = psramfs_object_append(fd, offset, (u8_t *)buf, remaining);
    PSRAMFS_CHECK_RES(res);
  }
  return len;

}
#endif // !PSRAMFS_READ_ONLY

s32_t PSRAMFS_write(psram *fs, psramfs_file fh, void *buf, s32_t len) {
  PSRAMFS_API_DBG("%s "_SPIPRIfd " "_SPIPRIi "\n", __func__, fh, len);
#if PSRAMFS_READ_ONLY
  (void)fs; (void)fh; (void)buf; (void)len;
  return PSRAMFS_ERR_RO_NOT_IMPL;
#else
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  psramfs_fd *fd;
  s32_t res;
  u32_t offset;

  fh = PSRAMFS_FH_UNOFFS(fs, fh);
  res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  if ((fd->flags & PSRAMFS_O_WRONLY) == 0) {
    res = PSRAMFS_ERR_NOT_WRITABLE;
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }

  if ((fd->flags & PSRAMFS_O_APPEND)) {
    fd->fdoffset = fd->size == PSRAMFS_UNDEFINED_LEN ? 0 : fd->size;
  }
  offset = fd->fdoffset;

#if PSRAMFS_CACHE_WR
  if (fd->cache_page == 0) {
    // see if object id is associated with cache already
    fd->cache_page = psramfs_cache_page_get_by_fd(fs, fd);
  }
#endif
  if (fd->flags & PSRAMFS_O_APPEND) {
    if (fd->size == PSRAMFS_UNDEFINED_LEN) {
      offset = 0;
    } else {
      offset = fd->size;
    }
#if PSRAMFS_CACHE_WR
    if (fd->cache_page) {
      offset = MAX(offset, fd->cache_page->offset + fd->cache_page->size);
    }
#endif
  }

#if PSRAMFS_CACHE_WR
  if ((fd->flags & PSRAMFS_O_DIRECT) == 0) {
    if (len < (s32_t)PSRAMFS_CFG_LOG_PAGE_SZ(fs)) {
      // small write, try to cache it
      u8_t alloc_cpage = 1;
      if (fd->cache_page) {
        // have a cached page for this fd already, check cache page boundaries
        if (offset < fd->cache_page->offset || // writing before cache
            offset > fd->cache_page->offset + fd->cache_page->size || // writing after cache
            offset + len > fd->cache_page->offset + PSRAMFS_CFG_LOG_PAGE_SZ(fs)) // writing beyond cache page
        {
          // boundary violation, write back cache first and allocate new
          PSRAMFS_CACHE_DBG("CACHE_WR_DUMP: dumping cache page "_SPIPRIi" for fd "_SPIPRIfd":"_SPIPRIid", boundary viol, offs:"_SPIPRIi" size:"_SPIPRIi"\n",
              fd->cache_page->ix, fd->file_nbr, fd->obj_id, fd->cache_page->offset, fd->cache_page->size);
          res = psramfs_hydro_write(fs, fd,
              psramfs_get_cache_page(fs, psramfs_get_cache(fs), fd->cache_page->ix),
              fd->cache_page->offset, fd->cache_page->size);
          psramfs_cache_fd_release(fs, fd->cache_page);
          PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
        } else {
          // writing within cache
          alloc_cpage = 0;
        }
      }

      if (alloc_cpage) {
        fd->cache_page = psramfs_cache_page_allocate_by_fd(fs, fd);
        if (fd->cache_page) {
          fd->cache_page->offset = offset;
          fd->cache_page->size = 0;
          PSRAMFS_CACHE_DBG("CACHE_WR_ALLO: allocating cache page "_SPIPRIi" for fd "_SPIPRIfd":"_SPIPRIid"\n",
              fd->cache_page->ix, fd->file_nbr, fd->obj_id);
        }
      }

      if (fd->cache_page) {
        u32_t offset_in_cpage = offset - fd->cache_page->offset;
        PSRAMFS_CACHE_DBG("CACHE_WR_WRITE: storing to cache page "_SPIPRIi" for fd "_SPIPRIfd":"_SPIPRIid", offs "_SPIPRIi":"_SPIPRIi" len "_SPIPRIi"\n",
            fd->cache_page->ix, fd->file_nbr, fd->obj_id,
            offset, offset_in_cpage, len);
        psramfs_cache *cache = psramfs_get_cache(fs);
        u8_t *cpage_data = psramfs_get_cache_page(fs, cache, fd->cache_page->ix);
#ifdef _PSRAMFS_TEST
        {
          intptr_t __a1 = (u8_t*)&cpage_data[offset_in_cpage]-(u8_t*)cache;
          intptr_t __a2 = (u8_t*)&cpage_data[offset_in_cpage]+len-(u8_t*)cache;
          intptr_t __b = sizeof(psramfs_cache) + cache->cpage_count * (sizeof(psramfs_cache_page) + PSRAMFS_CFG_LOG_PAGE_SZ(fs));
          if (__a1 > __b || __a2 > __b) {
            printf("FATAL OOB: CACHE_WR: memcpy to cache buffer ixs:%4ld..%4ld of %4ld\n", __a1, __a2, __b);
            ERREXIT();
          }
        }
#endif
        _PSRAMFS_MEMCPY(&cpage_data[offset_in_cpage], buf, len);
        fd->cache_page->size = MAX(fd->cache_page->size, offset_in_cpage + len);
        fd->fdoffset += len;
        PSRAMFS_UNLOCK(fs);
        return len;
      } else {
        res = psramfs_hydro_write(fs, fd, buf, offset, len);
        PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
        fd->fdoffset += len;
        PSRAMFS_UNLOCK(fs);
        return res;
      }
    } else {
      // big write, no need to cache it - but first check if there is a cached write already
      if (fd->cache_page) {
        // write back cache first
        PSRAMFS_CACHE_DBG("CACHE_WR_DUMP: dumping cache page "_SPIPRIi" for fd "_SPIPRIfd":"_SPIPRIid", big write, offs:"_SPIPRIi" size:"_SPIPRIi"\n",
            fd->cache_page->ix, fd->file_nbr, fd->obj_id, fd->cache_page->offset, fd->cache_page->size);
        res = psramfs_hydro_write(fs, fd,
            psramfs_get_cache_page(fs, psramfs_get_cache(fs), fd->cache_page->ix),
            fd->cache_page->offset, fd->cache_page->size);
        psramfs_cache_fd_release(fs, fd->cache_page);
        PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
        // data written below
      }
    }
  }
#endif

  res = psramfs_hydro_write(fs, fd, buf, offset, len);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  fd->fdoffset += len;

  PSRAMFS_UNLOCK(fs);

  return res;
#endif // PSRAMFS_READ_ONLY
}

s32_t PSRAMFS_lseek(psram *fs, psramfs_file fh, s32_t offs, int whence) {
  PSRAMFS_API_DBG("%s "_SPIPRIfd " "_SPIPRIi " %s\n", __func__, fh, offs, (const char* []){"SET","CUR","END","???"}[MIN(whence,3)]);
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  psramfs_fd *fd;
  s32_t res;
  fh = PSRAMFS_FH_UNOFFS(fs, fh);
  res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

#if PSRAMFS_CACHE_WR
  psramfs_fflush_cache(fs, fh);
#endif

  s32_t file_size = fd->size == PSRAMFS_UNDEFINED_LEN ? 0 : fd->size;

  switch (whence) {
  case PSRAMFS_SEEK_CUR:
    offs = fd->fdoffset+offs;
    break;
  case PSRAMFS_SEEK_END:
    offs = file_size + offs;
    break;
  }
  if (offs < 0) {
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, PSRAMFS_ERR_SEEK_BOUNDS);
  }
  if (offs > file_size) {
    fd->fdoffset = file_size;
    res = PSRAMFS_ERR_END_OF_OBJECT;
  }
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  psramfs_span_ix data_spix = (offs > 0 ? (offs-1) : 0) / PSRAMFS_DATA_PAGE_SIZE(fs);
  psramfs_span_ix objix_spix = PSRAMFS_OBJ_IX_ENTRY_SPAN_IX(fs, data_spix);
  if (fd->cursor_objix_spix != objix_spix) {
    psramfs_page_ix pix;
    res = psramfs_obj_lu_find_id_and_span(
        fs, fd->obj_id | PSRAMFS_OBJ_ID_IX_FLAG, objix_spix, 0, &pix);
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
    fd->cursor_objix_spix = objix_spix;
    fd->cursor_objix_pix = pix;
  }
  fd->fdoffset = offs;

  PSRAMFS_UNLOCK(fs);

  return offs;
}

s32_t PSRAMFS_remove(psram *fs, const char *path) {
  PSRAMFS_API_DBG("%s '%s'\n", __func__, path);
#if PSRAMFS_READ_ONLY
  (void)fs; (void)path;
  return PSRAMFS_ERR_RO_NOT_IMPL;
#else
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  if (strlen(path) > PSRAMFS_OBJ_NAME_LEN - 1) {
    PSRAMFS_API_CHECK_RES(fs, PSRAMFS_ERR_NAME_TOO_LONG);
  }
  PSRAMFS_LOCK(fs);

  psramfs_fd *fd;
  psramfs_page_ix pix;
  s32_t res;

  res = psramfs_fd_find_new(fs, &fd, 0);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_object_find_object_index_header_by_name(fs, (const u8_t*)path, &pix);
  if (res != PSRAMFS_OK) {
    psramfs_fd_return(fs, fd->file_nbr);
  }
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_object_open_by_page(fs, pix, fd, 0,0);
  if (res != PSRAMFS_OK) {
    psramfs_fd_return(fs, fd->file_nbr);
  }
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_object_truncate(fd, 0, 1);
  if (res != PSRAMFS_OK) {
    psramfs_fd_return(fs, fd->file_nbr);
  }
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  PSRAMFS_UNLOCK(fs);
  return 0;
#endif // PSRAMFS_READ_ONLY
}

s32_t PSRAMFS_fremove(psram *fs, psramfs_file fh) {
  PSRAMFS_API_DBG("%s "_SPIPRIfd "\n", __func__, fh);
#if PSRAMFS_READ_ONLY
  (void)fs; (void)fh;
  return PSRAMFS_ERR_RO_NOT_IMPL;
#else
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  psramfs_fd *fd;
  s32_t res;
  fh = PSRAMFS_FH_UNOFFS(fs, fh);
  res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  if ((fd->flags & PSRAMFS_O_WRONLY) == 0) {
    res = PSRAMFS_ERR_NOT_WRITABLE;
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }

#if PSRAMFS_CACHE_WR
  psramfs_cache_fd_release(fs, fd->cache_page);
#endif

  res = psramfs_object_truncate(fd, 0, 1);

  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  PSRAMFS_UNLOCK(fs);

  return 0;
#endif // PSRAMFS_READ_ONLY
}

s32_t PSRAMFS_ftruncate(psram* fs, psramfs_file fh, u32_t new_size) {
#if PSRAMFS_READ_ONLY
  (void)fs; (void)fh; (void)new_size;
  return PSRAMFS_ERR_RO_NOT_IMPL;
#else
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  psramfs_fd* fd;

  fh = PSRAMFS_FH_UNOFFS(fs, fh);
  s32_t res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  if ((fd->flags & PSRAMFS_O_WRONLY) == 0) {
    res = PSRAMFS_ERR_NOT_WRITABLE;
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }

#if PSRAMFS_CACHE_WR
  psramfs_fflush_cache(fs, fh);
#endif

  u32_t file_size = (fd->size == PSRAMFS_UNDEFINED_LEN) ? 0 : fd->size;
  if (new_size == file_size) {
    res = PSRAMFS_OK;
  } else if (new_size > file_size) {
    res = PSRAMFS_ERR_END_OF_OBJECT; // Same error we'd get from PSRAMFS_lseek
  } else {
    res = psramfs_object_truncate(fd, new_size, 0);
  }
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  PSRAMFS_UNLOCK(fs);
  return PSRAMFS_OK;
#endif
}

static s32_t psramfs_stat_pix(psram *fs, psramfs_page_ix pix, psramfs_file fh, psramfs_stat *s) {
  (void)fh;
  psramfs_page_object_ix_header objix_hdr;
  psramfs_obj_id obj_id;
  s32_t res =_psramfs_rd(fs,  PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_READ, fh,
      PSRAMFS_PAGE_TO_PADDR(fs, pix), sizeof(psramfs_page_object_ix_header), (u8_t *)&objix_hdr);
  PSRAMFS_API_CHECK_RES(fs, res);

  u32_t obj_id_addr = PSRAMFS_BLOCK_TO_PADDR(fs, PSRAMFS_BLOCK_FOR_PAGE(fs , pix)) +
      PSRAMFS_OBJ_LOOKUP_ENTRY_FOR_PAGE(fs, pix) * sizeof(psramfs_obj_id);
  res =_psramfs_rd(fs,  PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_READ, fh,
      obj_id_addr, sizeof(psramfs_obj_id), (u8_t *)&obj_id);
  PSRAMFS_API_CHECK_RES(fs, res);

  s->obj_id = obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG;
  s->type = objix_hdr.type;
  s->size = objix_hdr.size == PSRAMFS_UNDEFINED_LEN ? 0 : objix_hdr.size;
  s->pix = pix;
  strncpy((char *)s->name, (char *)objix_hdr.name, PSRAMFS_OBJ_NAME_LEN);
#if PSRAMFS_OBJ_META_LEN
  _PSRAMFS_MEMCPY(s->meta, objix_hdr.meta, PSRAMFS_OBJ_META_LEN);
#endif

  return res;
}

s32_t PSRAMFS_stat(psram *fs, const char *path, psramfs_stat *s) {
  PSRAMFS_API_DBG("%s '%s'\n", __func__, path);
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  if (strlen(path) > PSRAMFS_OBJ_NAME_LEN - 1) {
    PSRAMFS_API_CHECK_RES(fs, PSRAMFS_ERR_NAME_TOO_LONG);
  }
  PSRAMFS_LOCK(fs);

  s32_t res;
  psramfs_page_ix pix;

  res = psramfs_object_find_object_index_header_by_name(fs, (const u8_t*)path, &pix);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_stat_pix(fs, pix, 0, s);

  PSRAMFS_UNLOCK(fs);

  return res;
}

s32_t PSRAMFS_fstat(psram *fs, psramfs_file fh, psramfs_stat *s) {
  PSRAMFS_API_DBG("%s "_SPIPRIfd "\n", __func__, fh);
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  psramfs_fd *fd;
  s32_t res;

  fh = PSRAMFS_FH_UNOFFS(fs, fh);
  res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

#if PSRAMFS_CACHE_WR
  psramfs_fflush_cache(fs, fh);
#endif

  res = psramfs_stat_pix(fs, fd->objix_hdr_pix, fh, s);

  PSRAMFS_UNLOCK(fs);

  return res;
}

// Checks if there are any cached writes for the object id associated with
// given filehandle. If so, these writes are flushed.
#if PSRAMFS_CACHE == 1
static s32_t psramfs_fflush_cache(psram *fs, psramfs_file fh) {
  (void)fs;
  (void)fh;
  s32_t res = PSRAMFS_OK;
#if !PSRAMFS_READ_ONLY && PSRAMFS_CACHE_WR

  psramfs_fd *fd;
  res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES(fs, res);

  if ((fd->flags & PSRAMFS_O_DIRECT) == 0) {
    if (fd->cache_page == 0) {
      // see if object id is associated with cache already
      fd->cache_page = psramfs_cache_page_get_by_fd(fs, fd);
    }
    if (fd->cache_page) {
      PSRAMFS_CACHE_DBG("CACHE_WR_DUMP: dumping cache page "_SPIPRIi" for fd "_SPIPRIfd":"_SPIPRIid", flush, offs:"_SPIPRIi" size:"_SPIPRIi"\n",
          fd->cache_page->ix, fd->file_nbr,  fd->obj_id, fd->cache_page->offset, fd->cache_page->size);
      res = psramfs_hydro_write(fs, fd,
          psramfs_get_cache_page(fs, psramfs_get_cache(fs), fd->cache_page->ix),
          fd->cache_page->offset, fd->cache_page->size);
      if (res < PSRAMFS_OK) {
        fs->err_code = res;
      }
      psramfs_cache_fd_release(fs, fd->cache_page);
    }
  }
#endif

  return res;
}
#endif

s32_t PSRAMFS_fflush(psram *fs, psramfs_file fh) {
  PSRAMFS_API_DBG("%s "_SPIPRIfd "\n", __func__, fh);
  (void)fh;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  s32_t res = PSRAMFS_OK;
#if !PSRAMFS_READ_ONLY && PSRAMFS_CACHE_WR
  PSRAMFS_LOCK(fs);
  fh = PSRAMFS_FH_UNOFFS(fs, fh);
  res = psramfs_fflush_cache(fs, fh);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs,res);
  PSRAMFS_UNLOCK(fs);
#endif

  return res;
}

s32_t PSRAMFS_close(psram *fs, psramfs_file fh) {
  PSRAMFS_API_DBG("%s "_SPIPRIfd "\n", __func__, fh);
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);

  s32_t res = PSRAMFS_OK;
  PSRAMFS_LOCK(fs);

  fh = PSRAMFS_FH_UNOFFS(fs, fh);
#if PSRAMFS_CACHE
  res = psramfs_fflush_cache(fs, fh);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
#endif
  res = psramfs_fd_return(fs, fh);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  PSRAMFS_UNLOCK(fs);

  return res;
}

s32_t PSRAMFS_rename(psram *fs, const char *old_path, const char *new_path) {
  PSRAMFS_API_DBG("%s %s %s\n", __func__, old_path, new_path);
#if PSRAMFS_READ_ONLY
  (void)fs; (void)old_path; (void)new_path;
  return PSRAMFS_ERR_RO_NOT_IMPL;
#else
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  if (strlen(new_path) > PSRAMFS_OBJ_NAME_LEN - 1 ||
      strlen(old_path) > PSRAMFS_OBJ_NAME_LEN - 1) {
    PSRAMFS_API_CHECK_RES(fs, PSRAMFS_ERR_NAME_TOO_LONG);
  }
  PSRAMFS_LOCK(fs);

  psramfs_page_ix pix_old, pix_dummy;
  psramfs_fd *fd;

  s32_t res = psramfs_object_find_object_index_header_by_name(fs, (const u8_t*)old_path, &pix_old);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_object_find_object_index_header_by_name(fs, (const u8_t*)new_path, &pix_dummy);
  if (res == PSRAMFS_ERR_NOT_FOUND) {
    res = PSRAMFS_OK;
  } else if (res == PSRAMFS_OK) {
    res = PSRAMFS_ERR_CONFLICTING_NAME;
  }
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_fd_find_new(fs, &fd, 0);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_object_open_by_page(fs, pix_old, fd, 0, 0);
  if (res != PSRAMFS_OK) {
    psramfs_fd_return(fs, fd->file_nbr);
  }
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_object_update_index_hdr(fs, fd, fd->obj_id, fd->objix_hdr_pix, 0, (const u8_t*)new_path,
      0, 0, &pix_dummy);
#if PSRAMFS_TEMPORAL_FD_CACHE
  if (res == PSRAMFS_OK) {
    psramfs_fd_temporal_cache_rehash(fs, old_path, new_path);
  }
#endif

  psramfs_fd_return(fs, fd->file_nbr);

  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  PSRAMFS_UNLOCK(fs);

  return res;
#endif // PSRAMFS_READ_ONLY
}

#if PSRAMFS_OBJ_META_LEN
s32_t PSRAMFS_update_meta(psram *fs, const char *name, const void *meta) {
#if PSRAMFS_READ_ONLY
  (void)fs; (void)name; (void)meta;
  return PSRAMFS_ERR_RO_NOT_IMPL;
#else
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  psramfs_page_ix pix, pix_dummy;
  psramfs_fd *fd;

  s32_t res = psramfs_object_find_object_index_header_by_name(fs, (const u8_t*)name, &pix);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_fd_find_new(fs, &fd, 0);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_object_open_by_page(fs, pix, fd, 0, 0);
  if (res != PSRAMFS_OK) {
    psramfs_fd_return(fs, fd->file_nbr);
  }
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  res = psramfs_object_update_index_hdr(fs, fd, fd->obj_id, fd->objix_hdr_pix, 0, 0, meta,
      0, &pix_dummy);

  psramfs_fd_return(fs, fd->file_nbr);

  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  PSRAMFS_UNLOCK(fs);

  return res;
#endif // PSRAMFS_READ_ONLY
}

s32_t PSRAMFS_fupdate_meta(psram *fs, psramfs_file fh, const void *meta) {
#if PSRAMFS_READ_ONLY
  (void)fs; (void)fh; (void)meta;
  return PSRAMFS_ERR_RO_NOT_IMPL;
#else
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  s32_t res;
  psramfs_fd *fd;
  psramfs_page_ix pix_dummy;

  fh = PSRAMFS_FH_UNOFFS(fs, fh);
  res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  if ((fd->flags & PSRAMFS_O_WRONLY) == 0) {
    res = PSRAMFS_ERR_NOT_WRITABLE;
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  }

  res = psramfs_object_update_index_hdr(fs, fd, fd->obj_id, fd->objix_hdr_pix, 0, 0, meta,
      0, &pix_dummy);

  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  PSRAMFS_UNLOCK(fs);

  return res;
#endif // PSRAMFS_READ_ONLY
}
#endif // PSRAMFS_OBJ_META_LEN

psramfs_DIR *PSRAMFS_opendir(psram *fs, const char *name, psramfs_DIR *d) {
  PSRAMFS_API_DBG("%s\n", __func__);
  (void)name;

  if (!PSRAMFS_CHECK_CFG((fs))) {
    (fs)->err_code = PSRAMFS_ERR_NOT_CONFIGURED;
    return 0;
  }

  if (!PSRAMFS_CHECK_MOUNT(fs)) {
    fs->err_code = PSRAMFS_ERR_NOT_MOUNTED;
    return 0;
  }

  d->fs = fs;
  d->block = 0;
  d->entry = 0;
  return d;
}

static s32_t psramfs_read_dir_v(
    psram *fs,
    psramfs_obj_id obj_id,
    psramfs_block_ix bix,
    int ix_entry,
    const void *user_const_p,
    void *user_var_p) {
  (void)user_const_p;
  s32_t res;
  psramfs_page_object_ix_header objix_hdr;
  if (obj_id == PSRAMFS_OBJ_ID_FREE || obj_id == PSRAMFS_OBJ_ID_DELETED ||
      (obj_id & PSRAMFS_OBJ_ID_IX_FLAG) == 0) {
    return PSRAMFS_VIS_COUNTINUE;
  }

  psramfs_page_ix pix = PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, ix_entry);
  res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
      0, PSRAMFS_PAGE_TO_PADDR(fs, pix), sizeof(psramfs_page_object_ix_header), (u8_t *)&objix_hdr);
  if (res != PSRAMFS_OK) return res;
  if ((obj_id & PSRAMFS_OBJ_ID_IX_FLAG) &&
      objix_hdr.p_hdr.span_ix == 0 &&
      (objix_hdr.p_hdr.flags & (PSRAMFS_PH_FLAG_DELET | PSRAMFS_PH_FLAG_FINAL | PSRAMFS_PH_FLAG_IXDELE)) ==
          (PSRAMFS_PH_FLAG_DELET | PSRAMFS_PH_FLAG_IXDELE)) {
    struct psramfs_dirent *e = (struct psramfs_dirent*)user_var_p;
    e->obj_id = obj_id;
    strcpy((char *)e->name, (char *)objix_hdr.name);
    e->type = objix_hdr.type;
    e->size = objix_hdr.size == PSRAMFS_UNDEFINED_LEN ? 0 : objix_hdr.size;
    e->pix = pix;
#if PSRAMFS_OBJ_META_LEN
    _PSRAMFS_MEMCPY(e->meta, objix_hdr.meta, PSRAMFS_OBJ_META_LEN);
#endif
    return PSRAMFS_OK;
  }
  return PSRAMFS_VIS_COUNTINUE;
}

struct psramfs_dirent *PSRAMFS_readdir(psramfs_DIR *d, struct psramfs_dirent *e) {
  PSRAMFS_API_DBG("%s\n", __func__);
  if (!PSRAMFS_CHECK_MOUNT(d->fs)) {
    d->fs->err_code = PSRAMFS_ERR_NOT_MOUNTED;
    return 0;
  }
  PSRAMFS_LOCK(d->fs);

  psramfs_block_ix bix;
  int entry;
  s32_t res;
  struct psramfs_dirent *ret = 0;

  res = psramfs_obj_lu_find_entry_visitor(d->fs,
      d->block,
      d->entry,
      PSRAMFS_VIS_NO_WRAP,
      0,
      psramfs_read_dir_v,
      0,
      e,
      &bix,
      &entry);
  if (res == PSRAMFS_OK) {
    d->block = bix;
    d->entry = entry + 1;
    e->obj_id &= ~PSRAMFS_OBJ_ID_IX_FLAG;
    ret = e;
  } else if (res == PSRAMFS_VIS_END) {
    // end of iteration
  } else {
    d->fs->err_code = res;
  }
  PSRAMFS_UNLOCK(d->fs);
  return ret;
}

s32_t PSRAMFS_closedir(psramfs_DIR *d) {
  PSRAMFS_API_DBG("%s\n", __func__);
  PSRAMFS_API_CHECK_CFG(d->fs);
  PSRAMFS_API_CHECK_MOUNT(d->fs);
  return 0;
}

s32_t PSRAMFS_check(psram *fs) {
  PSRAMFS_API_DBG("%s\n", __func__);
#if PSRAMFS_READ_ONLY
  (void)fs;
  return PSRAMFS_ERR_RO_NOT_IMPL;
#else
  s32_t res;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  res = psramfs_lookup_consistency_check(fs, 0);

  res = psramfs_object_index_consistency_check(fs);

  res = psramfs_page_consistency_check(fs);

  res = psramfs_obj_lu_scan(fs);

  PSRAMFS_UNLOCK(fs);
  return res;
#endif // PSRAMFS_READ_ONLY
}

s32_t PSRAMFS_info(psram *fs, u32_t *total, u32_t *used) {
  PSRAMFS_API_DBG("%s\n", __func__);
  s32_t res = PSRAMFS_OK;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  u32_t pages_per_block = PSRAMFS_PAGES_PER_BLOCK(fs);
  u32_t blocks = fs->block_count;
  u32_t obj_lu_pages = PSRAMFS_OBJ_LOOKUP_PAGES(fs);
  u32_t data_page_size = PSRAMFS_DATA_PAGE_SIZE(fs);
  u32_t total_data_pages = (blocks - 2) * (pages_per_block - obj_lu_pages) + 1; // -2 for spare blocks, +1 for emergency page

  if (total) {
    *total = total_data_pages * data_page_size;
  }

  if (used) {
    *used = fs->stats_p_allocated * data_page_size;
  }

  PSRAMFS_UNLOCK(fs);
  return res;
}

s32_t PSRAMFS_gc_quick(psram *fs, u16_t max_free_pages) {
  PSRAMFS_API_DBG("%s "_SPIPRIi "\n", __func__, max_free_pages);
#if PSRAMFS_READ_ONLY
  (void)fs; (void)max_free_pages;
  return PSRAMFS_ERR_RO_NOT_IMPL;
#else
  s32_t res;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  res = psramfs_gc_quick(fs, max_free_pages);

  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  PSRAMFS_UNLOCK(fs);
  return 0;
#endif // PSRAMFS_READ_ONLY
}


s32_t PSRAMFS_gc(psram *fs, u32_t size) {
  PSRAMFS_API_DBG("%s "_SPIPRIi "\n", __func__, size);
#if PSRAMFS_READ_ONLY
  (void)fs; (void)size;
  return PSRAMFS_ERR_RO_NOT_IMPL;
#else
  s32_t res;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  res = psramfs_gc_check(fs, size);

  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
  PSRAMFS_UNLOCK(fs);
  return 0;
#endif // PSRAMFS_READ_ONLY
}

s32_t PSRAMFS_eof(psram *fs, psramfs_file fh) {
  PSRAMFS_API_DBG("%s "_SPIPRIfd "\n", __func__, fh);
  s32_t res;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  fh = PSRAMFS_FH_UNOFFS(fs, fh);

  psramfs_fd *fd;
  res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

#if PSRAMFS_CACHE_WR
  res = psramfs_fflush_cache(fs, fh);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
#endif

  res = (fd->fdoffset >= (fd->size == PSRAMFS_UNDEFINED_LEN ? 0 : fd->size));

  PSRAMFS_UNLOCK(fs);
  return res;
}

s32_t PSRAMFS_tell(psram *fs, psramfs_file fh) {
  PSRAMFS_API_DBG("%s "_SPIPRIfd "\n", __func__, fh);
  s32_t res;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  fh = PSRAMFS_FH_UNOFFS(fs, fh);

  psramfs_fd *fd;
  res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

#if PSRAMFS_CACHE_WR
  res = psramfs_fflush_cache(fs, fh);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
#endif

  res = fd->fdoffset;

  PSRAMFS_UNLOCK(fs);
  return res;
}

s32_t PSRAMFS_set_file_callback_func(psram *fs, psramfs_file_callback cb_func) {
  PSRAMFS_API_DBG("%s\n", __func__);
  PSRAMFS_LOCK(fs);
  fs->file_cb_f = cb_func;
  PSRAMFS_UNLOCK(fs);
  return 0;
}

#if PSRAMFS_IX_MAP

s32_t PSRAMFS_ix_map(psram *fs,  psramfs_file fh, psramfs_ix_map *map,
    u32_t offset, u32_t len, psramfs_page_ix *map_buf) {
  PSRAMFS_API_DBG("%s "_SPIPRIfd " "_SPIPRIi " "_SPIPRIi "\n", __func__, fh, offset, len);
  s32_t res;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  fh = PSRAMFS_FH_UNOFFS(fs, fh);

  psramfs_fd *fd;
  res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  if (fd->ix_map) {
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, PSRAMFS_ERR_IX_MAP_MAPPED);
  }

  map->map_buf = map_buf;
  map->offset = offset;
  // nb: spix range includes last
  map->start_spix = offset / PSRAMFS_DATA_PAGE_SIZE(fs);
  map->end_spix = (offset + len) / PSRAMFS_DATA_PAGE_SIZE(fs);
  memset(map_buf, 0, sizeof(psramfs_page_ix) * (map->end_spix - map->start_spix + 1));
  fd->ix_map = map;

  // scan for pixes
  res = psramfs_populate_ix_map(fs, fd, 0, map->end_spix - map->start_spix + 1);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  PSRAMFS_UNLOCK(fs);
  return res;
}

s32_t PSRAMFS_ix_unmap(psram *fs,  psramfs_file fh) {
  PSRAMFS_API_DBG("%s "_SPIPRIfd "\n", __func__, fh);
  s32_t res;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  fh = PSRAMFS_FH_UNOFFS(fs, fh);

  psramfs_fd *fd;
  res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  if (fd->ix_map == 0) {
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, PSRAMFS_ERR_IX_MAP_UNMAPPED);
  }

  fd->ix_map = 0;

  PSRAMFS_UNLOCK(fs);
  return res;
}

s32_t PSRAMFS_ix_remap(psram *fs, psramfs_file fh, u32_t offset) {
  PSRAMFS_API_DBG("%s "_SPIPRIfd " "_SPIPRIi "\n", __func__, fh, offset);
  s32_t res = PSRAMFS_OK;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  fh = PSRAMFS_FH_UNOFFS(fs, fh);

  psramfs_fd *fd;
  res = psramfs_fd_get(fs, fh, &fd);
  PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);

  if (fd->ix_map == 0) {
    PSRAMFS_API_CHECK_RES_UNLOCK(fs, PSRAMFS_ERR_IX_MAP_UNMAPPED);
  }

  psramfs_ix_map *map = fd->ix_map;

  s32_t spix_diff = offset / PSRAMFS_DATA_PAGE_SIZE(fs) - map->start_spix;
  map->offset = offset;

  // move existing pixes if within map offs
  if (spix_diff != 0) {
    // move vector
    int i;
    const s32_t vec_len = map->end_spix - map->start_spix + 1; // spix range includes last
    map->start_spix += spix_diff;
    map->end_spix += spix_diff;
    if (spix_diff >= vec_len) {
      // moving beyond range
      memset(&map->map_buf, 0, vec_len * sizeof(psramfs_page_ix));
      // populate_ix_map is inclusive
      res = psramfs_populate_ix_map(fs, fd, 0, vec_len-1);
      PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
    } else if (spix_diff > 0) {
      // diff positive
      for (i = 0; i < vec_len - spix_diff; i++) {
        map->map_buf[i] = map->map_buf[i + spix_diff];
      }
      // memset is non-inclusive
      memset(&map->map_buf[vec_len - spix_diff], 0, spix_diff * sizeof(psramfs_page_ix));
      // populate_ix_map is inclusive
      res = psramfs_populate_ix_map(fs, fd, vec_len - spix_diff, vec_len-1);
      PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
    } else {
      // diff negative
      for (i = vec_len - 1; i >= -spix_diff; i--) {
        map->map_buf[i] = map->map_buf[i + spix_diff];
      }
      // memset is non-inclusive
      memset(&map->map_buf[0], 0, -spix_diff * sizeof(psramfs_page_ix));
      // populate_ix_map is inclusive
      res = psramfs_populate_ix_map(fs, fd, 0, -spix_diff - 1);
      PSRAMFS_API_CHECK_RES_UNLOCK(fs, res);
    }

  }

  PSRAMFS_UNLOCK(fs);
  return res;
}

s32_t PSRAMFS_bytes_to_ix_map_entries(psram *fs, u32_t bytes) {
  PSRAMFS_API_CHECK_CFG(fs);
  // always add one extra page, the offset might change to the middle of a page
  return (bytes + PSRAMFS_DATA_PAGE_SIZE(fs) ) / PSRAMFS_DATA_PAGE_SIZE(fs);
}

s32_t PSRAMFS_ix_map_entries_to_bytes(psram *fs, u32_t map_page_ix_entries) {
  PSRAMFS_API_CHECK_CFG(fs);
  return map_page_ix_entries * PSRAMFS_DATA_PAGE_SIZE(fs);
}

#endif // PSRAMFS_IX_MAP

#if PSRAMFS_TEST_VISUALISATION
s32_t PSRAMFS_vis(psram *fs) {
  s32_t res = PSRAMFS_OK;
  PSRAMFS_API_CHECK_CFG(fs);
  PSRAMFS_API_CHECK_MOUNT(fs);
  PSRAMFS_LOCK(fs);

  int entries_per_page = (PSRAMFS_CFG_LOG_PAGE_SZ(fs) / sizeof(psramfs_obj_id));
  psramfs_obj_id *obj_lu_buf = (psramfs_obj_id *)fs->lu_work;
  psramfs_block_ix bix = 0;

  while (bix < fs->block_count) {
    // check each object lookup page
    int obj_lookup_page = 0;
    int cur_entry = 0;

    while (res == PSRAMFS_OK && obj_lookup_page < (int)PSRAMFS_OBJ_LOOKUP_PAGES(fs)) {
      int entry_offset = obj_lookup_page * entries_per_page;
      res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_READ,
          0, bix * PSRAMFS_CFG_LOG_BLOCK_SZ(fs) + PSRAMFS_PAGE_TO_PADDR(fs, obj_lookup_page), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->lu_work);
      // check each entry
      while (res == PSRAMFS_OK &&
          cur_entry - entry_offset < entries_per_page && cur_entry < (int)(PSRAMFS_PAGES_PER_BLOCK(fs)-PSRAMFS_OBJ_LOOKUP_PAGES(fs))) {
        psramfs_obj_id obj_id = obj_lu_buf[cur_entry-entry_offset];
        if (cur_entry == 0) {
          psramfs_printf(_SPIPRIbl" ", bix);
        } else if ((cur_entry & 0x3f) == 0) {
          psramfs_printf("     ");
        }
        if (obj_id == PSRAMFS_OBJ_ID_FREE) {
          psramfs_printf(PSRAMFS_TEST_VIS_FREE_STR);
        } else if (obj_id == PSRAMFS_OBJ_ID_DELETED) {
          psramfs_printf(PSRAMFS_TEST_VIS_DELE_STR);
        } else if (obj_id & PSRAMFS_OBJ_ID_IX_FLAG){
          psramfs_printf(PSRAMFS_TEST_VIS_INDX_STR(obj_id));
        } else {
          psramfs_printf(PSRAMFS_TEST_VIS_DATA_STR(obj_id));
        }
        cur_entry++;
        if ((cur_entry & 0x3f) == 0) {
          psramfs_printf("\n");
        }
      } // per entry
      obj_lookup_page++;
    } // per object lookup page

    psramfs_obj_id erase_count;
    res = _psramfs_rd(fs, PSRAMFS_OP_C_READ | PSRAMFS_OP_T_OBJ_LU2, 0,
        PSRAMFS_ERASE_COUNT_PADDR(fs, bix),
        sizeof(psramfs_obj_id), (u8_t *)&erase_count);
    PSRAMFS_CHECK_RES(res);

    if (erase_count != (psramfs_obj_id)-1) {
      psramfs_printf("\tera_cnt: "_SPIPRIi"\n", erase_count);
    } else {
      psramfs_printf("\tera_cnt: N/A\n");
    }

    bix++;
  } // per block

  psramfs_printf("era_cnt_max: "_SPIPRIi"\n", fs->max_erase_count);
  psramfs_printf("last_errno:  "_SPIPRIi"\n", fs->err_code);
  psramfs_printf("blocks:      "_SPIPRIi"\n", fs->block_count);
  psramfs_printf("free_blocks: "_SPIPRIi"\n", fs->free_blocks);
  psramfs_printf("page_alloc:  "_SPIPRIi"\n", fs->stats_p_allocated);
  psramfs_printf("page_delet:  "_SPIPRIi"\n", fs->stats_p_deleted);
  PSRAMFS_UNLOCK(fs);
  u32_t total = 0u, used = 0u;
  PSRAMFS_info(fs, &total, &used);
  psramfs_printf("used:        "_SPIPRIi" of "_SPIPRIi"\n", used, total);
  return res;
}
#endif
