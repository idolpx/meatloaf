/*
 * psramfs_cache.c
 *
 *  Created on: Jun 23, 2013
 *      Author: petera
 */

#include "psram.h"
#include "psramfs_nucleus.h"

#if PSRAMFS_CACHE

// returns cached page for give page index, or null if no such cached page
static psramfs_cache_page *psramfs_cache_page_get(psram *fs, psramfs_page_ix pix) {
  psramfs_cache *cache = psramfs_get_cache(fs);
  if ((cache->cpage_use_map & cache->cpage_use_mask) == 0) return 0;
  int i;
  for (i = 0; i < cache->cpage_count; i++) {
    psramfs_cache_page *cp = psramfs_get_cache_page_hdr(fs, cache, i);
    if ((cache->cpage_use_map & (1<<i)) &&
        (cp->flags & PSRAMFS_CACHE_FLAG_TYPE_WR) == 0 &&
        cp->pix == pix ) {
      //PSRAMFS_CACHE_DBG("CACHE_GET: have cache page "_SPIPRIi" for "_SPIPRIpg"\n", i, pix);
      cp->last_access = cache->last_access;
      return cp;
    }
  }
  //PSRAMFS_CACHE_DBG("CACHE_GET: no cache for "_SPIPRIpg"\n", pix);
  return 0;
}

// frees cached page
static s32_t psramfs_cache_page_free(psram *fs, int ix, u8_t write_back) {
  s32_t res = PSRAMFS_OK;
  psramfs_cache *cache = psramfs_get_cache(fs);
  psramfs_cache_page *cp = psramfs_get_cache_page_hdr(fs, cache, ix);
  if (cache->cpage_use_map & (1<<ix)) {
    if (write_back &&
        (cp->flags & PSRAMFS_CACHE_FLAG_TYPE_WR) == 0 &&
        (cp->flags & PSRAMFS_CACHE_FLAG_DIRTY)) {
      u8_t *mem =  psramfs_get_cache_page(fs, cache, ix);
      PSRAMFS_CACHE_DBG("CACHE_FREE: write cache page "_SPIPRIi" pix "_SPIPRIpg"\n", ix, cp->pix);
      res = PSRAMFS_HAL_WRITE(fs, PSRAMFS_PAGE_TO_PADDR(fs, cp->pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), mem);
    }

#if PSRAMFS_CACHE_WR
    if (cp->flags & PSRAMFS_CACHE_FLAG_TYPE_WR) {
      PSRAMFS_CACHE_DBG("CACHE_FREE: free cache page "_SPIPRIi" objid "_SPIPRIid"\n", ix, cp->obj_id);
    } else
#endif
    {
      PSRAMFS_CACHE_DBG("CACHE_FREE: free cache page "_SPIPRIi" pix "_SPIPRIpg"\n", ix, cp->pix);
    }
    cache->cpage_use_map &= ~(1 << ix);
    cp->flags = 0;
  }

  return res;
}

// removes the oldest accessed cached page
static s32_t psramfs_cache_page_remove_oldest(psram *fs, u8_t flag_mask, u8_t flags) {
  s32_t res = PSRAMFS_OK;
  psramfs_cache *cache = psramfs_get_cache(fs);

  if ((cache->cpage_use_map & cache->cpage_use_mask) != cache->cpage_use_mask) {
    // at least one free cpage
    return PSRAMFS_OK;
  }

  // all busy, scan thru all to find the cpage which has oldest access
  int i;
  int cand_ix = -1;
  u32_t oldest_val = 0;
  for (i = 0; i < cache->cpage_count; i++) {
    psramfs_cache_page *cp = psramfs_get_cache_page_hdr(fs, cache, i);
    if ((cache->last_access - cp->last_access) > oldest_val &&
        (cp->flags & flag_mask) == flags) {
      oldest_val = cache->last_access - cp->last_access;
      cand_ix = i;
    }
  }

  if (cand_ix >= 0) {
    res = psramfs_cache_page_free(fs, cand_ix, 1);
  }

  return res;
}

// allocates a new cached page and returns it, or null if all cache pages are busy
static psramfs_cache_page *psramfs_cache_page_allocate(psram *fs) {
  psramfs_cache *cache = psramfs_get_cache(fs);
  if (cache->cpage_use_map == 0xffffffff) {
    // out of cache memory
    return 0;
  }
  int i;
  for (i = 0; i < cache->cpage_count; i++) {
    if ((cache->cpage_use_map & (1<<i)) == 0) {
      psramfs_cache_page *cp = psramfs_get_cache_page_hdr(fs, cache, i);
      cache->cpage_use_map |= (1<<i);
      cp->last_access = cache->last_access;
      //PSRAMFS_CACHE_DBG("CACHE_ALLO: allocated cache page "_SPIPRIi"\n", i);
      return cp;
    }
  }
  // out of cache entries
  return 0;
}

// drops the cache page for give page index
void psramfs_cache_drop_page(psram *fs, psramfs_page_ix pix) {
  psramfs_cache_page *cp =  psramfs_cache_page_get(fs, pix);
  if (cp) {
    psramfs_cache_page_free(fs, cp->ix, 0);
  }
}

// ------------------------------

// reads from spi flash or the cache
s32_t psramfs_phys_rd(
    psram *fs,
    u8_t op,
    psramfs_file fh,
    u32_t addr,
    u32_t len,
    u8_t *dst) {
  (void)fh;
  s32_t res = PSRAMFS_OK;
  psramfs_cache *cache = psramfs_get_cache(fs);
  psramfs_cache_page *cp =  psramfs_cache_page_get(fs, PSRAMFS_PADDR_TO_PAGE(fs, addr));
  cache->last_access++;
  if (cp) {
    // we've already got one, you see
#if PSRAMFS_CACHE_STATS
    fs->cache_hits++;
#endif
    cp->last_access = cache->last_access;
    u8_t *mem =  psramfs_get_cache_page(fs, cache, cp->ix);
    _PSRAMFS_MEMCPY(dst, &mem[PSRAMFS_PADDR_TO_PAGE_OFFSET(fs, addr)], len);
  } else {
    if ((op & PSRAMFS_OP_TYPE_MASK) == PSRAMFS_OP_T_OBJ_LU2) {
      // for second layer lookup functions, we do not cache in order to prevent shredding
      return PSRAMFS_HAL_READ(fs, addr, len, dst);
    }
#if PSRAMFS_CACHE_STATS
    fs->cache_misses++;
#endif
    // this operation will always free one cache page (unless all already free),
    // the result code stems from the write operation of the possibly freed cache page
    res = psramfs_cache_page_remove_oldest(fs, PSRAMFS_CACHE_FLAG_TYPE_WR, 0);

    cp = psramfs_cache_page_allocate(fs);
    if (cp) {
      cp->flags = PSRAMFS_CACHE_FLAG_WRTHRU;
      cp->pix = PSRAMFS_PADDR_TO_PAGE(fs, addr);
      PSRAMFS_CACHE_DBG("CACHE_ALLO: allocated cache page "_SPIPRIi" for pix "_SPIPRIpg "\n", cp->ix, cp->pix);

      s32_t res2 = PSRAMFS_HAL_READ(fs,
          addr - PSRAMFS_PADDR_TO_PAGE_OFFSET(fs, addr),
          PSRAMFS_CFG_LOG_PAGE_SZ(fs),
          psramfs_get_cache_page(fs, cache, cp->ix));
      if (res2 != PSRAMFS_OK) {
        // honor read failure before possible write failure (bad idea?)
        res = res2;
      }
      u8_t *mem =  psramfs_get_cache_page(fs, cache, cp->ix);
      _PSRAMFS_MEMCPY(dst, &mem[PSRAMFS_PADDR_TO_PAGE_OFFSET(fs, addr)], len);
    } else {
      // this will never happen, last resort for sake of symmetry
      s32_t res2 = PSRAMFS_HAL_READ(fs, addr, len, dst);
      if (res2 != PSRAMFS_OK) {
        // honor read failure before possible write failure (bad idea?)
        res = res2;
      }
    }
  }
  return res;
}

// writes to spi flash and/or the cache
s32_t psramfs_phys_wr(
    psram *fs,
    u8_t op,
    psramfs_file fh,
    u32_t addr,
    u32_t len,
    u8_t *src) {
  (void)fh;
  psramfs_page_ix pix = PSRAMFS_PADDR_TO_PAGE(fs, addr);
  psramfs_cache *cache = psramfs_get_cache(fs);
  psramfs_cache_page *cp =  psramfs_cache_page_get(fs, pix);

  if (cp && (op & PSRAMFS_OP_COM_MASK) != PSRAMFS_OP_C_WRTHRU) {
    // have a cache page
    // copy in data to cache page

    if ((op & PSRAMFS_OP_COM_MASK) == PSRAMFS_OP_C_DELE &&
        (op & PSRAMFS_OP_TYPE_MASK) != PSRAMFS_OP_T_OBJ_LU) {
      // page is being deleted, wipe from cache - unless it is a lookup page
      psramfs_cache_page_free(fs, cp->ix, 0);
      return PSRAMFS_HAL_WRITE(fs, addr, len, src);
    }

    u8_t *mem =  psramfs_get_cache_page(fs, cache, cp->ix);
    _PSRAMFS_MEMCPY(&mem[PSRAMFS_PADDR_TO_PAGE_OFFSET(fs, addr)], src, len);

    cache->last_access++;
    cp->last_access = cache->last_access;

    if (cp->flags & PSRAMFS_CACHE_FLAG_WRTHRU) {
      // page is being updated, no write-cache, just pass thru
      return PSRAMFS_HAL_WRITE(fs, addr, len, src);
    } else {
      return PSRAMFS_OK;
    }
  } else {
    // no cache page, no write cache - just write thru
    return PSRAMFS_HAL_WRITE(fs, addr, len, src);
  }
}

#if PSRAMFS_CACHE_WR
// returns the cache page that this fd refers, or null if no cache page
psramfs_cache_page *psramfs_cache_page_get_by_fd(psram *fs, psramfs_fd *fd) {
  psramfs_cache *cache = psramfs_get_cache(fs);

  if ((cache->cpage_use_map & cache->cpage_use_mask) == 0) {
    // all cpages free, no cpage cannot be assigned to obj_id
    return 0;
  }

  int i;
  for (i = 0; i < cache->cpage_count; i++) {
    psramfs_cache_page *cp = psramfs_get_cache_page_hdr(fs, cache, i);
    if ((cache->cpage_use_map & (1<<i)) &&
        (cp->flags & PSRAMFS_CACHE_FLAG_TYPE_WR) &&
        cp->obj_id == fd->obj_id) {
      return cp;
    }
  }

  return 0;
}

// allocates a new cache page and refers this to given fd - flushes an old cache
// page if all cache is busy
psramfs_cache_page *psramfs_cache_page_allocate_by_fd(psram *fs, psramfs_fd *fd) {
  // before this function is called, it is ensured that there is no already existing
  // cache page with same object id
  psramfs_cache_page_remove_oldest(fs, PSRAMFS_CACHE_FLAG_TYPE_WR, 0);
  psramfs_cache_page *cp = psramfs_cache_page_allocate(fs);
  if (cp == 0) {
    // could not get cache page
    return 0;
  }

  cp->flags = PSRAMFS_CACHE_FLAG_TYPE_WR;
  cp->obj_id = fd->obj_id;
  fd->cache_page = cp;
  PSRAMFS_CACHE_DBG("CACHE_ALLO: allocated cache page "_SPIPRIi" for fd "_SPIPRIfd ":"_SPIPRIid "\n", cp->ix, fd->file_nbr, fd->obj_id);
  return cp;
}

// unrefers all fds that this cache page refers to and releases the cache page
void psramfs_cache_fd_release(psram *fs, psramfs_cache_page *cp) {
  if (cp == 0) return;
  u32_t i;
  psramfs_fd *fds = (psramfs_fd *)fs->fd_space;
  for (i = 0; i < fs->fd_count; i++) {
    psramfs_fd *cur_fd = &fds[i];
    if (cur_fd->file_nbr != 0 && cur_fd->cache_page == cp) {
      cur_fd->cache_page = 0;
    }
  }
  psramfs_cache_page_free(fs, cp->ix, 0);

  cp->obj_id = 0;
}

#endif

// initializes the cache
void psramfs_cache_init(psram *fs) {
  if (fs->cache == 0) return;
  u32_t sz = fs->cache_size;
  u32_t cache_mask = 0;
  int i;
  int cache_entries =
      (sz - sizeof(psramfs_cache)) / (PSRAMFS_CACHE_PAGE_SIZE(fs));
  if (cache_entries <= 0) return;

  for (i = 0; i < cache_entries; i++) {
    cache_mask <<= 1;
    cache_mask |= 1;
  }

  psramfs_cache cache;
  memset(&cache, 0, sizeof(psramfs_cache));
  cache.cpage_count = cache_entries;
  cache.cpages = (u8_t *)((u8_t *)fs->cache + sizeof(psramfs_cache));

  cache.cpage_use_map = 0xffffffff;
  cache.cpage_use_mask = cache_mask;
  _PSRAMFS_MEMCPY(fs->cache, &cache, sizeof(psramfs_cache));

  psramfs_cache *c = psramfs_get_cache(fs);

  memset(c->cpages, 0, c->cpage_count * PSRAMFS_CACHE_PAGE_SIZE(fs));

  c->cpage_use_map &= ~(c->cpage_use_mask);
  for (i = 0; i < cache.cpage_count; i++) {
    psramfs_get_cache_page_hdr(fs, c, i)->ix = i;
  }
}

#endif // PSRAMFS_CACHE
