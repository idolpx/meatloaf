#include "psramfs.h"
#include "psramfs_nucleus.h"

static s32_t psramfs_page_data_check(psram *fs, psramfs_fd *fd, psramfs_page_ix pix, psramfs_span_ix spix) {
  s32_t res = PSRAMFS_OK;
  if (pix == (psramfs_page_ix)-1) {
    // referring to page 0xffff...., bad object index
    return PSRAMFS_ERR_INDEX_REF_FREE;
  }
  if (pix % PSRAMFS_PAGES_PER_BLOCK(fs) < PSRAMFS_OBJ_LOOKUP_PAGES(fs)) {
    // referring to an object lookup page, bad object index
    return PSRAMFS_ERR_INDEX_REF_LU;
  }
  if (pix > PSRAMFS_MAX_PAGES(fs)) {
    // referring to a bad page
    return PSRAMFS_ERR_INDEX_REF_INVALID;
  }
#if PSRAMFS_PAGE_CHECK
  psramfs_page_header ph;
  res = _psramfs_rd(
      fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_READ,
      fd->file_nbr,
      PSRAMFS_PAGE_TO_PADDR(fs, pix),
      sizeof(psramfs_page_header),
      (u8_t *)&ph);
  PSRAMFS_CHECK_RES(res);
  PSRAMFS_VALIDATE_DATA(ph, fd->obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG, spix);
#endif
  return res;
}

#if !PSRAMFS_READ_ONLY
static s32_t psramfs_page_index_check(psram *fs, psramfs_fd *fd, psramfs_page_ix pix, psramfs_span_ix spix) {
  s32_t res = PSRAMFS_OK;
  if (pix == (psramfs_page_ix)-1) {
    // referring to page 0xffff...., bad object index
    return PSRAMFS_ERR_INDEX_FREE;
  }
  if (pix % PSRAMFS_PAGES_PER_BLOCK(fs) < PSRAMFS_OBJ_LOOKUP_PAGES(fs)) {
    // referring to an object lookup page, bad object index
    return PSRAMFS_ERR_INDEX_LU;
  }
  if (pix > PSRAMFS_MAX_PAGES(fs)) {
    // referring to a bad page
    return PSRAMFS_ERR_INDEX_INVALID;
  }
#if PSRAMFS_PAGE_CHECK
  psramfs_page_header ph;
  res = _psramfs_rd(
      fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_READ,
      fd->file_nbr,
      PSRAMFS_PAGE_TO_PADDR(fs, pix),
      sizeof(psramfs_page_header),
      (u8_t *)&ph);
  PSRAMFS_CHECK_RES(res);
  PSRAMFS_VALIDATE_OBJIX(ph, fd->obj_id, spix);
#endif
  return res;
}
#endif // !PSRAMFS_READ_ONLY

#if !PSRAMFS_CACHE

s32_t psramfs_phys_rd(
    psram *fs,
    u32_t addr,
    u32_t len,
    u8_t *dst) {
  return PSRAMFS_HAL_READ(fs, addr, len, dst);
}

s32_t psramfs_phys_wr(
    psram *fs,
    u32_t addr,
    u32_t len,
    u8_t *src) {
  return PSRAMFS_HAL_WRITE(fs, addr, len, src);
}

#endif

#if !PSRAMFS_READ_ONLY
s32_t psramfs_phys_cpy(
    psram *fs,
    psramfs_file fh,
    u32_t dst,
    u32_t src,
    u32_t len) {
  (void)fh;
  s32_t res;
  u8_t b[PSRAMFS_COPY_BUFFER_STACK];
  while (len > 0) {
    u32_t chunk_size = MIN(PSRAMFS_COPY_BUFFER_STACK, len);
    res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_MOVS, fh, src, chunk_size, b);
    PSRAMFS_CHECK_RES(res);
    res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_MOVD,  fh, dst, chunk_size, b);
    PSRAMFS_CHECK_RES(res);
    len -= chunk_size;
    src += chunk_size;
    dst += chunk_size;
  }
  return PSRAMFS_OK;
}
#endif // !PSRAMFS_READ_ONLY

// Find object lookup entry containing given id with visitor.
// Iterate over object lookup pages in each block until a given object id entry is found.
// When found, the visitor function is called with block index, entry index and user data.
// If visitor returns PSRAMFS_VIS_CONTINUE, the search goes on. Otherwise, the search will be
// ended and visitor's return code is returned to caller.
// If no visitor is given (0) the search returns on first entry with matching object id.
// If no match is found in all look up, PSRAMFS_VIS_END is returned.
// @param fs                    the file system
// @param starting_block        the starting block to start search in
// @param starting_lu_entry     the look up index entry to start search in
// @param flags                 ored combination of PSRAMFS_VIS_CHECK_ID, PSRAMFS_VIS_CHECK_PH,
//                              PSRAMFS_VIS_NO_WRAP
// @param obj_id                argument object id
// @param v                     visitor callback function
// @param user_const_p          any const pointer, passed to the callback visitor function
// @param user_var_p            any pointer, passed to the callback visitor function
// @param block_ix              reported block index where match was found
// @param lu_entry              reported look up index where match was found
s32_t psramfs_obj_lu_find_entry_visitor(
    psram *fs,
    psramfs_block_ix starting_block,
    int starting_lu_entry,
    u8_t flags,
    psramfs_obj_id obj_id,
    psramfs_visitor_f v,
    const void *user_const_p,
    void *user_var_p,
    psramfs_block_ix *block_ix,
    int *lu_entry) {
  s32_t res = PSRAMFS_OK;
  s32_t entry_count = fs->block_count * PSRAMFS_OBJ_LOOKUP_MAX_ENTRIES(fs);
  psramfs_block_ix cur_block = starting_block;
  u32_t cur_block_addr = starting_block * PSRAMFS_CFG_LOG_BLOCK_SZ(fs);

  psramfs_obj_id *obj_lu_buf = (psramfs_obj_id *)fs->lu_work;
  int cur_entry = starting_lu_entry;
  int entries_per_page = (PSRAMFS_CFG_LOG_PAGE_SZ(fs) / sizeof(psramfs_obj_id));

  // wrap initial
  if (cur_entry > (int)PSRAMFS_OBJ_LOOKUP_MAX_ENTRIES(fs) - 1) {
    cur_entry = 0;
    cur_block++;
    cur_block_addr = cur_block * PSRAMFS_CFG_LOG_BLOCK_SZ(fs);
    if (cur_block >= fs->block_count) {
      if (flags & PSRAMFS_VIS_NO_WRAP) {
        return PSRAMFS_VIS_END;
      } else {
        // block wrap
        cur_block = 0;
        cur_block_addr = 0;
      }
    }
  }

  // check each block
  while (res == PSRAMFS_OK && entry_count > 0) {
    int obj_lookup_page = cur_entry / entries_per_page;
    // check each object lookup page
    while (res == PSRAMFS_OK && obj_lookup_page < (int)PSRAMFS_OBJ_LOOKUP_PAGES(fs)) {
      int entry_offset = obj_lookup_page * entries_per_page;
      res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_READ,
          0, cur_block_addr + PSRAMFS_PAGE_TO_PADDR(fs, obj_lookup_page), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->lu_work);
      // check each entry
      while (res == PSRAMFS_OK &&
          cur_entry - entry_offset < entries_per_page && // for non-last obj lookup pages
          cur_entry < (int)PSRAMFS_OBJ_LOOKUP_MAX_ENTRIES(fs)) // for last obj lookup page
      {
        if ((flags & PSRAMFS_VIS_CHECK_ID) == 0 || obj_lu_buf[cur_entry-entry_offset] == obj_id) {
          if (block_ix) *block_ix = cur_block;
          if (lu_entry) *lu_entry = cur_entry;
          if (v) {
            res = v(
                fs,
                (flags & PSRAMFS_VIS_CHECK_PH) ? obj_id : obj_lu_buf[cur_entry-entry_offset],
                cur_block,
                cur_entry,
                user_const_p,
                user_var_p);
            if (res == PSRAMFS_VIS_COUNTINUE || res == PSRAMFS_VIS_COUNTINUE_RELOAD) {
              if (res == PSRAMFS_VIS_COUNTINUE_RELOAD) {
                res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_READ,
                    0, cur_block_addr + PSRAMFS_PAGE_TO_PADDR(fs, obj_lookup_page), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->lu_work);
                PSRAMFS_CHECK_RES(res);
              }
              res = PSRAMFS_OK;
              cur_entry++;
              entry_count--;
              continue;
            } else {
              return res;
            }
          } else {
            return PSRAMFS_OK;
          }
        }
        entry_count--;
        cur_entry++;
      } // per entry
      obj_lookup_page++;
    } // per object lookup page
    cur_entry = 0;
    cur_block++;
    cur_block_addr += PSRAMFS_CFG_LOG_BLOCK_SZ(fs);
    if (cur_block >= fs->block_count) {
      if (flags & PSRAMFS_VIS_NO_WRAP) {
        return PSRAMFS_VIS_END;
      } else {
        // block wrap
        cur_block = 0;
        cur_block_addr = 0;
      }
    }
  } // per block

  PSRAMFS_CHECK_RES(res);

  return PSRAMFS_VIS_END;
}

#if !PSRAMFS_READ_ONLY
s32_t psramfs_erase_block(
    psram *fs,
    psramfs_block_ix bix) {
  s32_t res;
  u32_t addr = PSRAMFS_BLOCK_TO_PADDR(fs, bix);
  s32_t size = PSRAMFS_CFG_LOG_BLOCK_SZ(fs);

  // here we ignore res, just try erasing the block
  while (size > 0) {
    PSRAMFS_DBG("erase "_SPIPRIad":"_SPIPRIi"\n", addr,  PSRAMFS_CFG_PHYS_ERASE_SZ(fs));
    PSRAMFS_HAL_ERASE(fs, addr, PSRAMFS_CFG_PHYS_ERASE_SZ(fs));

    addr += PSRAMFS_CFG_PHYS_ERASE_SZ(fs);
    size -= PSRAMFS_CFG_PHYS_ERASE_SZ(fs);
  }
  fs->free_blocks++;

  // register erase count for this block
  res = _psramfs_wr(fs, PSRAMFS_OP_C_WRTHRU | PSRAMFS_OP_T_OBJ_LU2, 0,
      PSRAMFS_ERASE_COUNT_PADDR(fs, bix),
      sizeof(psramfs_obj_id), (u8_t *)&fs->max_erase_count);
  PSRAMFS_CHECK_RES(res);

#if PSRAMFS_USE_MAGIC
  // finally, write magic
  psramfs_obj_id magic = PSRAMFS_MAGIC(fs, bix);
  res = _psramfs_wr(fs, PSRAMFS_OP_C_WRTHRU | PSRAMFS_OP_T_OBJ_LU2, 0,
      PSRAMFS_MAGIC_PADDR(fs, bix),
      sizeof(psramfs_obj_id), (u8_t *)&magic);
  PSRAMFS_CHECK_RES(res);
#endif

  fs->max_erase_count++;
  if (fs->max_erase_count == PSRAMFS_OBJ_ID_IX_FLAG) {
    fs->max_erase_count = 0;
  }

  return res;
}
#endif // !PSRAMFS_READ_ONLY

#if PSRAMFS_USE_MAGIC && PSRAMFS_USE_MAGIC_LENGTH && PSRAMFS_SINGLETON==0
s32_t psramfs_probe(
    psramfs_config *cfg) {
  s32_t res;
  u32_t paddr;
  psram dummy_fs; // create a dummy fs struct just to be able to use macros
  _PSRAMFS_MEMCPY(&dummy_fs.cfg, cfg, sizeof(psramfs_config));
  dummy_fs.block_count = 0;

  // Read three magics, as one block may be in an aborted erase state.
  // At least two of these must contain magic and be in decreasing order.
  psramfs_obj_id magic[3];
  psramfs_obj_id bix_count[3];

  psramfs_block_ix bix;
  for (bix = 0; bix < 3; bix++) {
    paddr = PSRAMFS_MAGIC_PADDR(&dummy_fs, bix);
#if PSRAMFS_HAL_CALLBACK_EXTRA
    // not any proper fs to report here, so callback with null
    // (cross fingers that no-one gets angry)
    res = cfg->hal_read_f((void *)0, paddr, sizeof(psramfs_obj_id), (u8_t *)&magic[bix]);
#else
    res = cfg->hal_read_f(paddr, sizeof(psramfs_obj_id), (u8_t *)&magic[bix]);
#endif
    bix_count[bix] = magic[bix] ^ PSRAMFS_MAGIC(&dummy_fs, 0);
    PSRAMFS_CHECK_RES(res);
  }

  // check that we have sane number of blocks
  if (bix_count[0] < 3) return PSRAMFS_ERR_PROBE_TOO_FEW_BLOCKS;
  // check that the order is correct, take aborted erases in calculation
  // first block aborted erase
  if (magic[0] == (psramfs_obj_id)(-1) && bix_count[1] - bix_count[2] == 1) {
    return (bix_count[1]+1) * cfg->log_block_size;
  }
  // second block aborted erase
  if (magic[1] == (psramfs_obj_id)(-1) && bix_count[0] - bix_count[2] == 2) {
    return bix_count[0] * cfg->log_block_size;
  }
  // third block aborted erase
  if (magic[2] == (psramfs_obj_id)(-1) && bix_count[0] - bix_count[1] == 1) {
    return bix_count[0] * cfg->log_block_size;
  }
  // no block has aborted erase
  if (bix_count[0] - bix_count[1] == 1 && bix_count[1] - bix_count[2] == 1) {
    return bix_count[0] * cfg->log_block_size;
  }

  return PSRAMFS_ERR_PROBE_NOT_A_FS;
}
#endif // PSRAMFS_USE_MAGIC && PSRAMFS_USE_MAGIC_LENGTH && PSRAMFS_SINGLETON==0


static s32_t psramfs_obj_lu_scan_v(
    psram *fs,
    psramfs_obj_id obj_id,
    psramfs_block_ix bix,
    int ix_entry,
    const void *user_const_p,
    void *user_var_p) {
  (void)bix;
  (void)user_const_p;
  (void)user_var_p;
  if (obj_id == PSRAMFS_OBJ_ID_FREE) {
    if (ix_entry == 0) {
      fs->free_blocks++;
      // todo optimize further, return PSRAMFS_NEXT_BLOCK
    }
  } else if (obj_id == PSRAMFS_OBJ_ID_DELETED) {
    fs->stats_p_deleted++;
  } else {
    fs->stats_p_allocated++;
  }

  return PSRAMFS_VIS_COUNTINUE;
}


// Scans thru all obj lu and counts free, deleted and used pages
// Find the maximum block erase count
// Checks magic if enabled
s32_t psramfs_obj_lu_scan(
    psram *fs) {
  s32_t res;
  psramfs_block_ix bix;
  int entry;
#if PSRAMFS_USE_MAGIC
  psramfs_block_ix unerased_bix = (psramfs_block_ix)-1;
#endif

  // find out erase count
  // if enabled, check magic
  bix = 0;
  psramfs_obj_id erase_count_final;
  psramfs_obj_id erase_count_min = PSRAMFS_OBJ_ID_FREE;
  psramfs_obj_id erase_count_max = 0;
  while (bix < fs->block_count) {
#if PSRAMFS_USE_MAGIC
    psramfs_obj_id magic;
    res = _psramfs_rd(fs,
        PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
        0, PSRAMFS_MAGIC_PADDR(fs, bix) ,
        sizeof(psramfs_obj_id), (u8_t *)&magic);

    PSRAMFS_CHECK_RES(res);
    if (magic != PSRAMFS_MAGIC(fs, bix)) {
      if (unerased_bix == (psramfs_block_ix)-1) {
        // allow one unerased block as it might be powered down during an erase
        unerased_bix = bix;
      } else {
        // more than one unerased block, bail out
        PSRAMFS_CHECK_RES(PSRAMFS_ERR_NOT_A_FS);
      }
    }
#endif
    psramfs_obj_id erase_count;
    res = _psramfs_rd(fs,
        PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
        0, PSRAMFS_ERASE_COUNT_PADDR(fs, bix) ,
        sizeof(psramfs_obj_id), (u8_t *)&erase_count);
    PSRAMFS_CHECK_RES(res);
    if (erase_count != PSRAMFS_OBJ_ID_FREE) {
      erase_count_min = MIN(erase_count_min, erase_count);
      erase_count_max = MAX(erase_count_max, erase_count);
    }
    bix++;
  }

  if (erase_count_min == 0 && erase_count_max == PSRAMFS_OBJ_ID_FREE) {
    // clean system, set counter to zero
    erase_count_final = 0;
  } else if (erase_count_max - erase_count_min > (PSRAMFS_OBJ_ID_FREE)/2) {
    // wrap, take min
    erase_count_final = erase_count_min+1;
  } else {
    erase_count_final = erase_count_max+1;
  }

  fs->max_erase_count = erase_count_final;

#if PSRAMFS_USE_MAGIC
  if (unerased_bix != (psramfs_block_ix)-1) {
    // found one unerased block, remedy
    PSRAMFS_DBG("mount: erase block "_SPIPRIbl"\n", bix);
#if PSRAMFS_READ_ONLY
    res = PSRAMFS_ERR_RO_ABORTED_OPERATION;
#else
    res = psramfs_erase_block(fs, unerased_bix);
#endif // PSRAMFS_READ_ONLY
    PSRAMFS_CHECK_RES(res);
  }
#endif

  // count blocks

  fs->free_blocks = 0;
  fs->stats_p_allocated = 0;
  fs->stats_p_deleted = 0;

  res = psramfs_obj_lu_find_entry_visitor(fs,
      0,
      0,
      0,
      0,
      psramfs_obj_lu_scan_v,
      0,
      0,
      &bix,
      &entry);

  if (res == PSRAMFS_VIS_END) {
    res = PSRAMFS_OK;
  }

  PSRAMFS_CHECK_RES(res);

  return res;
}

#if !PSRAMFS_READ_ONLY
// Find free object lookup entry
// Iterate over object lookup pages in each block until a free object id entry is found
s32_t psramfs_obj_lu_find_free(
    psram *fs,
    psramfs_block_ix starting_block,
    int starting_lu_entry,
    psramfs_block_ix *block_ix,
    int *lu_entry) {
  s32_t res;
  if (!fs->cleaning && fs->free_blocks < 2) {
    res = psramfs_gc_quick(fs, 0);
    if (res == PSRAMFS_ERR_NO_DELETED_BLOCKS) {
      res = PSRAMFS_OK;
    }
    PSRAMFS_CHECK_RES(res);
    if (fs->free_blocks < 2) {
      return PSRAMFS_ERR_FULL;
    }
  }
  res = psramfs_obj_lu_find_id(fs, starting_block, starting_lu_entry,
      PSRAMFS_OBJ_ID_FREE, block_ix, lu_entry);
  if (res == PSRAMFS_OK) {
    fs->free_cursor_block_ix = *block_ix;
    fs->free_cursor_obj_lu_entry = (*lu_entry) + 1;
    if (*lu_entry == 0) {
      fs->free_blocks--;
    }
  }
  if (res == PSRAMFS_ERR_FULL) {
    PSRAMFS_DBG("fs full\n");
  }

  return res;
}
#endif // !PSRAMFS_READ_ONLY

// Find object lookup entry containing given id
// Iterate over object lookup pages in each block until a given object id entry is found
s32_t psramfs_obj_lu_find_id(
    psram *fs,
    psramfs_block_ix starting_block,
    int starting_lu_entry,
    psramfs_obj_id obj_id,
    psramfs_block_ix *block_ix,
    int *lu_entry) {
  s32_t res = psramfs_obj_lu_find_entry_visitor(
      fs, starting_block, starting_lu_entry, PSRAMFS_VIS_CHECK_ID, obj_id, 0, 0, 0, block_ix, lu_entry);
  if (res == PSRAMFS_VIS_END) {
    res = PSRAMFS_ERR_NOT_FOUND;
  }
  return res;
}


static s32_t psramfs_obj_lu_find_id_and_span_v(
    psram *fs,
    psramfs_obj_id obj_id,
    psramfs_block_ix bix,
    int ix_entry,
    const void *user_const_p,
    void *user_var_p) {
  s32_t res;
  psramfs_page_header ph;
  psramfs_page_ix pix = PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, ix_entry);
  res = _psramfs_rd(fs, 0, PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
      PSRAMFS_PAGE_TO_PADDR(fs, pix), sizeof(psramfs_page_header), (u8_t *)&ph);
  PSRAMFS_CHECK_RES(res);
  if (ph.obj_id == obj_id &&
      ph.span_ix == *((psramfs_span_ix*)user_var_p) &&
      (ph.flags & (PSRAMFS_PH_FLAG_FINAL | PSRAMFS_PH_FLAG_DELET | PSRAMFS_PH_FLAG_USED)) == PSRAMFS_PH_FLAG_DELET &&
      !((obj_id & PSRAMFS_OBJ_ID_IX_FLAG) && (ph.flags & PSRAMFS_PH_FLAG_IXDELE) == 0 && ph.span_ix == 0) &&
      (user_const_p == 0 || *((const psramfs_page_ix*)user_const_p) != pix)) {
    return PSRAMFS_OK;
  } else {
    return PSRAMFS_VIS_COUNTINUE;
  }
}

// Find object lookup entry containing given id and span index
// Iterate over object lookup pages in each block until a given object id entry is found
s32_t psramfs_obj_lu_find_id_and_span(
    psram *fs,
    psramfs_obj_id obj_id,
    psramfs_span_ix spix,
    psramfs_page_ix exclusion_pix,
    psramfs_page_ix *pix) {
  s32_t res;
  psramfs_block_ix bix;
  int entry;

  res = psramfs_obj_lu_find_entry_visitor(fs,
      fs->cursor_block_ix,
      fs->cursor_obj_lu_entry,
      PSRAMFS_VIS_CHECK_ID,
      obj_id,
      psramfs_obj_lu_find_id_and_span_v,
      exclusion_pix ? &exclusion_pix : 0,
      &spix,
      &bix,
      &entry);

  if (res == PSRAMFS_VIS_END) {
    res = PSRAMFS_ERR_NOT_FOUND;
  }

  PSRAMFS_CHECK_RES(res);

  if (pix) {
    *pix = PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, entry);
  }

  fs->cursor_block_ix = bix;
  fs->cursor_obj_lu_entry = entry;

  return res;
}

// Find object lookup entry containing given id and span index in page headers only
// Iterate over object lookup pages in each block until a given object id entry is found
s32_t psramfs_obj_lu_find_id_and_span_by_phdr(
    psram *fs,
    psramfs_obj_id obj_id,
    psramfs_span_ix spix,
    psramfs_page_ix exclusion_pix,
    psramfs_page_ix *pix) {
  s32_t res;
  psramfs_block_ix bix;
  int entry;

  res = psramfs_obj_lu_find_entry_visitor(fs,
      fs->cursor_block_ix,
      fs->cursor_obj_lu_entry,
      PSRAMFS_VIS_CHECK_PH,
      obj_id,
      psramfs_obj_lu_find_id_and_span_v,
      exclusion_pix ? &exclusion_pix : 0,
      &spix,
      &bix,
      &entry);

  if (res == PSRAMFS_VIS_END) {
    res = PSRAMFS_ERR_NOT_FOUND;
  }

  PSRAMFS_CHECK_RES(res);

  if (pix) {
    *pix = PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, entry);
  }

  fs->cursor_block_ix = bix;
  fs->cursor_obj_lu_entry = entry;

  return res;
}

#if PSRAMFS_IX_MAP

// update index map of given fd with given object index data
static void psramfs_update_ix_map(psram *fs,
    psramfs_fd *fd, psramfs_span_ix objix_spix, psramfs_page_object_ix *objix) {
#if PSRAMFS_SINGLETON
  (void)fs;
#endif
  psramfs_ix_map *map = fd->ix_map;
  psramfs_span_ix map_objix_start_spix = PSRAMFS_OBJ_IX_ENTRY_SPAN_IX(fs, map->start_spix);
  psramfs_span_ix map_objix_end_spix = PSRAMFS_OBJ_IX_ENTRY_SPAN_IX(fs, map->end_spix);

  // check if updated ix is within map range
  if (objix_spix < map_objix_start_spix || objix_spix > map_objix_end_spix) {
    return;
  }

  // update memory mapped page index buffer to new pages

  // get range of updated object index map data span indices
  psramfs_span_ix objix_data_spix_start =
      PSRAMFS_DATA_SPAN_IX_FOR_OBJ_IX_SPAN_IX(fs, objix_spix);
  psramfs_span_ix objix_data_spix_end = objix_data_spix_start +
      (objix_spix == 0 ? PSRAMFS_OBJ_HDR_IX_LEN(fs) : PSRAMFS_OBJ_IX_LEN(fs));

  // calc union of object index range and index map range array
  psramfs_span_ix map_spix = MAX(map->start_spix, objix_data_spix_start);
  psramfs_span_ix map_spix_end = MIN(map->end_spix + 1, objix_data_spix_end);

  while (map_spix < map_spix_end) {
    psramfs_page_ix objix_data_pix;
    if (objix_spix == 0) {
      // get data page from object index header page
      objix_data_pix = ((psramfs_page_ix*)((u8_t *)objix + sizeof(psramfs_page_object_ix_header)))[map_spix];
    } else {
      // get data page from object index page
      objix_data_pix = ((psramfs_page_ix*)((u8_t *)objix + sizeof(psramfs_page_object_ix)))[PSRAMFS_OBJ_IX_ENTRY(fs, map_spix)];
    }

    if (objix_data_pix == (psramfs_page_ix)-1) {
      // reached end of object, abort
      break;
    }

    map->map_buf[map_spix - map->start_spix] = objix_data_pix;
    PSRAMFS_DBG("map "_SPIPRIid":"_SPIPRIsp" ("_SPIPRIsp"--"_SPIPRIsp") objix.spix:"_SPIPRIsp" to pix "_SPIPRIpg"\n",
        fd->obj_id, map_spix - map->start_spix,
        map->start_spix, map->end_spix,
        objix->p_hdr.span_ix,
        objix_data_pix);

    map_spix++;
  }
}

typedef struct {
  psramfs_fd *fd;
  u32_t remaining_objix_pages_to_visit;
  psramfs_span_ix map_objix_start_spix;
  psramfs_span_ix map_objix_end_spix;
} psramfs_ix_map_populate_state;

static s32_t psramfs_populate_ix_map_v(
    psram *fs,
    psramfs_obj_id obj_id,
    psramfs_block_ix bix,
    int ix_entry,
    const void *user_const_p,
    void *user_var_p) {
  (void)user_const_p;
  s32_t res;
  psramfs_ix_map_populate_state *state = (psramfs_ix_map_populate_state *)user_var_p;
  psramfs_page_ix pix = PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, ix_entry);

  // load header to check it
  psramfs_page_object_ix *objix = (psramfs_page_object_ix *)fs->work;
  res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
      0, PSRAMFS_PAGE_TO_PADDR(fs, pix), sizeof(psramfs_page_object_ix), (u8_t *)objix);
  PSRAMFS_CHECK_RES(res);
  PSRAMFS_VALIDATE_OBJIX(objix->p_hdr, obj_id, objix->p_hdr.span_ix);

  // check if hdr is ok, and if objix range overlap with ix map range
  if ((objix->p_hdr.flags & (PSRAMFS_PH_FLAG_DELET | PSRAMFS_PH_FLAG_FINAL | PSRAMFS_PH_FLAG_IXDELE)) ==
      (PSRAMFS_PH_FLAG_DELET | PSRAMFS_PH_FLAG_IXDELE) &&
      objix->p_hdr.span_ix >= state->map_objix_start_spix &&
      objix->p_hdr.span_ix <= state->map_objix_end_spix) {
    // ok, load rest of object index
    res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
        0, PSRAMFS_PAGE_TO_PADDR(fs, pix) + sizeof(psramfs_page_object_ix),
        PSRAMFS_CFG_LOG_PAGE_SZ(fs) - sizeof(psramfs_page_object_ix),
        (u8_t *)objix + sizeof(psramfs_page_object_ix));
    PSRAMFS_CHECK_RES(res);

    psramfs_update_ix_map(fs, state->fd, objix->p_hdr.span_ix, objix);

    state->remaining_objix_pages_to_visit--;
    PSRAMFS_DBG("map "_SPIPRIid" ("_SPIPRIsp"--"_SPIPRIsp") remaining objix pages "_SPIPRIi"\n",
        state->fd->obj_id,
        state->fd->ix_map->start_spix, state->fd->ix_map->end_spix,
        state->remaining_objix_pages_to_visit);
  }

  if (res == PSRAMFS_OK) {
    res = state->remaining_objix_pages_to_visit ? PSRAMFS_VIS_COUNTINUE : PSRAMFS_VIS_END;
  }
  return res;
}

// populates index map, from vector entry start to vector entry end, inclusive
s32_t psramfs_populate_ix_map(psram *fs, psramfs_fd *fd, u32_t vec_entry_start, u32_t vec_entry_end) {
  s32_t res;
  psramfs_ix_map *map = fd->ix_map;
  psramfs_ix_map_populate_state state;
  vec_entry_start = MIN((u32_t)(map->end_spix - map->start_spix), vec_entry_start);
  vec_entry_end = MAX((u32_t)(map->end_spix - map->start_spix), vec_entry_end);
  if (vec_entry_start > vec_entry_end) {
    return PSRAMFS_ERR_IX_MAP_BAD_RANGE;
  }
  state.map_objix_start_spix = PSRAMFS_OBJ_IX_ENTRY_SPAN_IX(fs, map->start_spix + vec_entry_start);
  state.map_objix_end_spix = PSRAMFS_OBJ_IX_ENTRY_SPAN_IX(fs, map->start_spix + vec_entry_end);
  state.remaining_objix_pages_to_visit =
      state.map_objix_end_spix - state.map_objix_start_spix + 1;
  state.fd = fd;

  res = psramfs_obj_lu_find_entry_visitor(
      fs,
      PSRAMFS_BLOCK_FOR_PAGE(fs, fd->objix_hdr_pix),
      PSRAMFS_OBJ_LOOKUP_ENTRY_FOR_PAGE(fs, fd->objix_hdr_pix),
      PSRAMFS_VIS_CHECK_ID,
      fd->obj_id | PSRAMFS_OBJ_ID_IX_FLAG,
      psramfs_populate_ix_map_v,
      0,
      &state,
      0,
      0);

  if (res == PSRAMFS_VIS_END) {
    res = PSRAMFS_OK;
  }

  return res;
}

#endif


#if !PSRAMFS_READ_ONLY
// Allocates a free defined page with given obj_id
// Occupies object lookup entry and page
// data may be NULL; where only page header is stored, len and page_offs is ignored
s32_t psramfs_page_allocate_data(
    psram *fs,
    psramfs_obj_id obj_id,
    psramfs_page_header *ph,
    u8_t *data,
    u32_t len,
    u32_t page_offs,
    u8_t finalize,
    psramfs_page_ix *pix) {
  s32_t res = PSRAMFS_OK;
  psramfs_block_ix bix;
  int entry;

  // find free entry
  res = psramfs_obj_lu_find_free(fs, fs->free_cursor_block_ix, fs->free_cursor_obj_lu_entry, &bix, &entry);
  PSRAMFS_CHECK_RES(res);

  // occupy page in object lookup
  res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_UPDT,
      0, PSRAMFS_BLOCK_TO_PADDR(fs, bix) + entry * sizeof(psramfs_obj_id), sizeof(psramfs_obj_id), (u8_t*)&obj_id);
  PSRAMFS_CHECK_RES(res);

  fs->stats_p_allocated++;

  // write page header
  ph->flags &= ~PSRAMFS_PH_FLAG_USED;
  res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_UPDT,
      0, PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PADDR(fs, bix, entry), sizeof(psramfs_page_header), (u8_t*)ph);
  PSRAMFS_CHECK_RES(res);

  // write page data
  if (data) {
    res = _psramfs_wr(fs,  PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_UPDT,
        0,PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PADDR(fs, bix, entry) + sizeof(psramfs_page_header) + page_offs, len, data);
    PSRAMFS_CHECK_RES(res);
  }

  // finalize header if necessary
  if (finalize && (ph->flags & PSRAMFS_PH_FLAG_FINAL)) {
    ph->flags &= ~PSRAMFS_PH_FLAG_FINAL;
    res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_UPDT,
        0, PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PADDR(fs, bix, entry) + offsetof(psramfs_page_header, flags),
        sizeof(u8_t),
        (u8_t *)&ph->flags);
    PSRAMFS_CHECK_RES(res);
  }

  // return written page
  if (pix) {
    *pix = PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, entry);
  }

  return res;
}
#endif // !PSRAMFS_READ_ONLY

#if !PSRAMFS_READ_ONLY
// Moves a page from src to a free page and finalizes it. Updates page index. Page data is given in param page.
// If page data is null, provided header is used for metainfo and page data is physically copied.
s32_t psramfs_page_move(
    psram *fs,
    psramfs_file fh,
    u8_t *page_data,
    psramfs_obj_id obj_id,
    psramfs_page_header *page_hdr,
    psramfs_page_ix src_pix,
    psramfs_page_ix *dst_pix) {
  s32_t res;
  u8_t was_final = 0;
  psramfs_page_header *p_hdr;
  psramfs_block_ix bix;
  int entry;
  psramfs_page_ix free_pix;

  // find free entry
  res = psramfs_obj_lu_find_free(fs, fs->free_cursor_block_ix, fs->free_cursor_obj_lu_entry, &bix, &entry);
  PSRAMFS_CHECK_RES(res);
  free_pix = PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, entry);

  if (dst_pix) *dst_pix = free_pix;

  p_hdr = page_data ? (psramfs_page_header *)page_data : page_hdr;
  if (page_data) {
    // got page data
    was_final = (p_hdr->flags & PSRAMFS_PH_FLAG_FINAL) == 0;
    // write unfinalized page
    p_hdr->flags |= PSRAMFS_PH_FLAG_FINAL;
    p_hdr->flags &= ~PSRAMFS_PH_FLAG_USED;
    res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_UPDT,
        0, PSRAMFS_PAGE_TO_PADDR(fs, free_pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), page_data);
  } else {
    // copy page data
    res = psramfs_phys_cpy(fs, fh, PSRAMFS_PAGE_TO_PADDR(fs, free_pix), PSRAMFS_PAGE_TO_PADDR(fs, src_pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs));
  }
  PSRAMFS_CHECK_RES(res);

  // mark entry in destination object lookup
  res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_UPDT,
      0, PSRAMFS_BLOCK_TO_PADDR(fs, PSRAMFS_BLOCK_FOR_PAGE(fs, free_pix)) + PSRAMFS_OBJ_LOOKUP_ENTRY_FOR_PAGE(fs, free_pix) * sizeof(psramfs_page_ix),
      sizeof(psramfs_obj_id),
      (u8_t *)&obj_id);
  PSRAMFS_CHECK_RES(res);

  fs->stats_p_allocated++;

  if (was_final) {
    // mark finalized in destination page
    p_hdr->flags &= ~(PSRAMFS_PH_FLAG_FINAL | PSRAMFS_PH_FLAG_USED);
    res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_UPDT,
        fh,
        PSRAMFS_PAGE_TO_PADDR(fs, free_pix) + offsetof(psramfs_page_header, flags),
        sizeof(u8_t),
        (u8_t *)&p_hdr->flags);
    PSRAMFS_CHECK_RES(res);
  }
  // mark source deleted
  res = psramfs_page_delete(fs, src_pix);
  return res;
}
#endif // !PSRAMFS_READ_ONLY

#if !PSRAMFS_READ_ONLY
// Deletes a page and removes it from object lookup.
s32_t psramfs_page_delete(
    psram *fs,
    psramfs_page_ix pix) {
  s32_t res;
  // mark deleted entry in source object lookup
  psramfs_obj_id d_obj_id = PSRAMFS_OBJ_ID_DELETED;
  res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_DELE,
      0,
      PSRAMFS_BLOCK_TO_PADDR(fs, PSRAMFS_BLOCK_FOR_PAGE(fs, pix)) + PSRAMFS_OBJ_LOOKUP_ENTRY_FOR_PAGE(fs, pix) * sizeof(psramfs_page_ix),
      sizeof(psramfs_obj_id),
      (u8_t *)&d_obj_id);
  PSRAMFS_CHECK_RES(res);

  fs->stats_p_deleted++;
  fs->stats_p_allocated--;

#if PSRAMFS_SECURE_ERASE
  // Secure erase
  unsigned char data[PSRAMFS_CFG_LOG_PAGE_SZ(fs) - sizeof(psramfs_page_header)];
  bzero(data, sizeof(data));
  res = _psramfs_wr(fs,  PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_DELE,
      0,
      PSRAMFS_PAGE_TO_PADDR(fs, pix) + sizeof(psramfs_page_header), sizeof(data), data);
  PSRAMFS_CHECK_RES(res);
#endif

  // mark deleted in source page
  u8_t flags = 0xff;
#if PSRAMFS_NO_BLIND_WRITES
  res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_READ,
      0, PSRAMFS_PAGE_TO_PADDR(fs, pix) + offsetof(psramfs_page_header, flags),
      sizeof(flags), &flags);
  PSRAMFS_CHECK_RES(res);
#endif
  flags &= ~(PSRAMFS_PH_FLAG_DELET | PSRAMFS_PH_FLAG_USED);
  res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_DELE,
      0,
      PSRAMFS_PAGE_TO_PADDR(fs, pix) + offsetof(psramfs_page_header, flags),
      sizeof(flags), &flags);

  return res;
}
#endif // !PSRAMFS_READ_ONLY

#if !PSRAMFS_READ_ONLY
// Create an object index header page with empty index and undefined length
s32_t psramfs_object_create(
    psram *fs,
    psramfs_obj_id obj_id,
    const u8_t name[],
    const u8_t meta[],
    psramfs_obj_type type,
    psramfs_page_ix *objix_hdr_pix) {
  s32_t res = PSRAMFS_OK;
  psramfs_block_ix bix;
  psramfs_page_object_ix_header oix_hdr;
  int entry;

  res = psramfs_gc_check(fs, PSRAMFS_DATA_PAGE_SIZE(fs));
  PSRAMFS_CHECK_RES(res);

  obj_id |= PSRAMFS_OBJ_ID_IX_FLAG;

  // find free entry
  res = psramfs_obj_lu_find_free(fs, fs->free_cursor_block_ix, fs->free_cursor_obj_lu_entry, &bix, &entry);
  PSRAMFS_CHECK_RES(res);
  PSRAMFS_DBG("create: found free page @ "_SPIPRIpg" bix:"_SPIPRIbl" entry:"_SPIPRIsp"\n", (psramfs_page_ix)PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, entry), bix, entry);

  // occupy page in object lookup
  res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_UPDT,
      0, PSRAMFS_BLOCK_TO_PADDR(fs, bix) + entry * sizeof(psramfs_obj_id), sizeof(psramfs_obj_id), (u8_t*)&obj_id);
  PSRAMFS_CHECK_RES(res);

  fs->stats_p_allocated++;

  // write empty object index page
  oix_hdr.p_hdr.obj_id = obj_id;
  oix_hdr.p_hdr.span_ix = 0;
  oix_hdr.p_hdr.flags = 0xff & ~(PSRAMFS_PH_FLAG_FINAL | PSRAMFS_PH_FLAG_INDEX | PSRAMFS_PH_FLAG_USED);
  oix_hdr.type = type;
  oix_hdr.size = PSRAMFS_UNDEFINED_LEN; // keep ones so we can update later without wasting this page
  strncpy((char*)oix_hdr.name, (const char*)name, sizeof(oix_hdr.name) - 1);
  ((char*)oix_hdr.name)[sizeof(oix_hdr.name) - 1] = '\0';
#if PSRAMFS_OBJ_META_LEN
  if (meta) {
    _PSRAMFS_MEMCPY(oix_hdr.meta, meta, PSRAMFS_OBJ_META_LEN);
  } else {
    memset(oix_hdr.meta, 0xff, PSRAMFS_OBJ_META_LEN);
  }
#else
  (void) meta;
#endif

  // update page
  res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_UPDT,
      0, PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PADDR(fs, bix, entry), sizeof(psramfs_page_object_ix_header), (u8_t*)&oix_hdr);

  PSRAMFS_CHECK_RES(res);
  psramfs_cb_object_event(fs, (psramfs_page_object_ix *)&oix_hdr,
      PSRAMFS_EV_IX_NEW, obj_id, 0, PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, entry), PSRAMFS_UNDEFINED_LEN);

  if (objix_hdr_pix) {
    *objix_hdr_pix = PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, entry);
  }

  return res;
}
#endif // !PSRAMFS_READ_ONLY

#if !PSRAMFS_READ_ONLY
// update object index header with any combination of name/size/index
// new_objix_hdr_data may be null, if so the object index header page is loaded
// name may be null, if so name is not changed
// size may be null, if so size is not changed
s32_t psramfs_object_update_index_hdr(
    psram *fs,
    psramfs_fd *fd,
    psramfs_obj_id obj_id,
    psramfs_page_ix objix_hdr_pix,
    u8_t *new_objix_hdr_data,
    const u8_t name[],
    const u8_t meta[],
    u32_t size,
    psramfs_page_ix *new_pix) {
  s32_t res = PSRAMFS_OK;
  psramfs_page_object_ix_header *objix_hdr;
  psramfs_page_ix new_objix_hdr_pix;

  obj_id |=  PSRAMFS_OBJ_ID_IX_FLAG;

  if (new_objix_hdr_data) {
    // object index header page already given to us, no need to load it
    objix_hdr = (psramfs_page_object_ix_header *)new_objix_hdr_data;
  } else {
    // read object index header page
    res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_READ,
        fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, objix_hdr_pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->work);
    PSRAMFS_CHECK_RES(res);
    objix_hdr = (psramfs_page_object_ix_header *)fs->work;
  }

  PSRAMFS_VALIDATE_OBJIX(objix_hdr->p_hdr, obj_id, 0);

  // change name
  if (name) {
    strncpy((char*)objix_hdr->name, (const char*)name, sizeof(objix_hdr->name) - 1);
    ((char*) objix_hdr->name)[sizeof(objix_hdr->name) - 1] = '\0';
  }
#if PSRAMFS_OBJ_META_LEN
  if (meta) {
    _PSRAMFS_MEMCPY(objix_hdr->meta, meta, PSRAMFS_OBJ_META_LEN);
  }
#else
  (void) meta;
#endif
  if (size) {
    objix_hdr->size = size;
  }

  // move and update page
  res = psramfs_page_move(fs, fd == 0 ? 0 : fd->file_nbr, (u8_t*)objix_hdr, obj_id, 0, objix_hdr_pix, &new_objix_hdr_pix);

  if (res == PSRAMFS_OK) {
    if (new_pix) {
      *new_pix = new_objix_hdr_pix;
    }
    // callback on object index update
    psramfs_cb_object_event(fs, (psramfs_page_object_ix *)objix_hdr,
        new_objix_hdr_data ? PSRAMFS_EV_IX_UPD : PSRAMFS_EV_IX_UPD_HDR,
            obj_id, objix_hdr->p_hdr.span_ix, new_objix_hdr_pix, objix_hdr->size);
    if (fd) fd->objix_hdr_pix = new_objix_hdr_pix; // if this is not in the registered cluster
  }

  return res;
}
#endif // !PSRAMFS_READ_ONLY

void psramfs_cb_object_event(
    psram *fs,
    psramfs_page_object_ix *objix,
    int ev,
    psramfs_obj_id obj_id_raw,
    psramfs_span_ix spix,
    psramfs_page_ix new_pix,
    u32_t new_size) {
#if PSRAMFS_IX_MAP == 0
  (void)objix;
#endif
  // update index caches in all file descriptors
  psramfs_obj_id obj_id = obj_id_raw & ~PSRAMFS_OBJ_ID_IX_FLAG;
  u32_t i;
  psramfs_fd *fds = (psramfs_fd *)fs->fd_space;
  PSRAMFS_DBG("       CALLBACK  %s obj_id:"_SPIPRIid" spix:"_SPIPRIsp" npix:"_SPIPRIpg" nsz:"_SPIPRIi"\n", (const char *[]){"UPD", "NEW", "DEL", "MOV", "HUP","???"}[MIN(ev,5)],
      obj_id_raw, spix, new_pix, new_size);
  for (i = 0; i < fs->fd_count; i++) {
    psramfs_fd *cur_fd = &fds[i];
    if ((cur_fd->obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG) != obj_id) continue; // fd not related to updated file
#if !PSRAMFS_TEMPORAL_FD_CACHE
    if (cur_fd->file_nbr == 0) continue; // fd closed
#endif
    if (spix == 0) { // object index header update
      if (ev != PSRAMFS_EV_IX_DEL) {
#if PSRAMFS_TEMPORAL_FD_CACHE
        if (cur_fd->score == 0) continue; // never used fd
#endif
        PSRAMFS_DBG("       callback: setting fd "_SPIPRIfd":"_SPIPRIid"(fdoffs:"_SPIPRIi" offs:"_SPIPRIi") objix_hdr_pix to "_SPIPRIpg", size:"_SPIPRIi"\n",
            PSRAMFS_FH_OFFS(fs, cur_fd->file_nbr), cur_fd->obj_id, cur_fd->fdoffset, cur_fd->offset, new_pix, new_size);
        cur_fd->objix_hdr_pix = new_pix;
        if (new_size != 0) {
          // update size and offsets for fds to this file
          cur_fd->size = new_size;
          u32_t act_new_size = new_size == PSRAMFS_UNDEFINED_LEN ? 0 : new_size;
#if PSRAMFS_CACHE_WR
          if (act_new_size > 0 && cur_fd->cache_page) {
            act_new_size = MAX(act_new_size, cur_fd->cache_page->offset + cur_fd->cache_page->size);
          }
#endif
          if (cur_fd->offset > act_new_size) {
            cur_fd->offset = act_new_size;
          }
          if (cur_fd->fdoffset > act_new_size) {
            cur_fd->fdoffset = act_new_size;
          }
#if PSRAMFS_CACHE_WR
          if (cur_fd->cache_page && cur_fd->cache_page->offset > act_new_size+1) {
            PSRAMFS_CACHE_DBG("CACHE_DROP: file trunced, dropping cache page "_SPIPRIi", no writeback\n", cur_fd->cache_page->ix);
            psramfs_cache_fd_release(fs, cur_fd->cache_page);
          }
#endif
        }
      } else {
        // removing file
#if PSRAMFS_CACHE_WR
        if (cur_fd->file_nbr && cur_fd->cache_page) {
          PSRAMFS_CACHE_DBG("CACHE_DROP: file deleted, dropping cache page "_SPIPRIi", no writeback\n", cur_fd->cache_page->ix);
          psramfs_cache_fd_release(fs, cur_fd->cache_page);
        }
#endif
        PSRAMFS_DBG("       callback: release fd "_SPIPRIfd":"_SPIPRIid" span:"_SPIPRIsp" objix_pix to "_SPIPRIpg"\n", PSRAMFS_FH_OFFS(fs, cur_fd->file_nbr), cur_fd->obj_id, spix, new_pix);
        cur_fd->file_nbr = 0;
        cur_fd->obj_id = PSRAMFS_OBJ_ID_DELETED;
      }
    } // object index header update
    if (cur_fd->cursor_objix_spix == spix) {
      if (ev != PSRAMFS_EV_IX_DEL) {
        PSRAMFS_DBG("       callback: setting fd "_SPIPRIfd":"_SPIPRIid" span:"_SPIPRIsp" objix_pix to "_SPIPRIpg"\n", PSRAMFS_FH_OFFS(fs, cur_fd->file_nbr), cur_fd->obj_id, spix, new_pix);
        cur_fd->cursor_objix_pix = new_pix;
      } else {
        cur_fd->cursor_objix_pix = 0;
      }
    }
  } // fd update loop

#if PSRAMFS_IX_MAP

  // update index maps
  if (ev == PSRAMFS_EV_IX_UPD || ev == PSRAMFS_EV_IX_NEW) {
    for (i = 0; i < fs->fd_count; i++) {
      psramfs_fd *cur_fd = &fds[i];
      // check fd opened, having ix map, match obj id
      if (cur_fd->file_nbr == 0 ||
          cur_fd->ix_map == 0 ||
          (cur_fd->obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG) != obj_id) continue;
      PSRAMFS_DBG("       callback: map ix update fd "_SPIPRIfd":"_SPIPRIid" span:"_SPIPRIsp"\n", PSRAMFS_FH_OFFS(fs, cur_fd->file_nbr), cur_fd->obj_id, spix);
      psramfs_update_ix_map(fs, cur_fd, spix, objix);
    }
  }

#endif

  // callback to user if object index header
  if (fs->file_cb_f && spix == 0 && (obj_id_raw & PSRAMFS_OBJ_ID_IX_FLAG)) {
    psramfs_fileop_type op;
    if (ev == PSRAMFS_EV_IX_NEW) {
      op = PSRAMFS_CB_CREATED;
    } else if (ev == PSRAMFS_EV_IX_UPD ||
        ev == PSRAMFS_EV_IX_MOV ||
        ev == PSRAMFS_EV_IX_UPD_HDR) {
      op = PSRAMFS_CB_UPDATED;
    } else if (ev == PSRAMFS_EV_IX_DEL) {
      op = PSRAMFS_CB_DELETED;
    } else {
      PSRAMFS_DBG("       callback: WARNING unknown callback event "_SPIPRIi"\n", ev);
      return; // bail out
    }
    fs->file_cb_f(fs, op, obj_id, new_pix);
  }
}

// Open object by id
s32_t psramfs_object_open_by_id(
    psram *fs,
    psramfs_obj_id obj_id,
    psramfs_fd *fd,
    psramfs_flags flags,
    psramfs_mode mode) {
  s32_t res = PSRAMFS_OK;
  psramfs_page_ix pix;

  res = psramfs_obj_lu_find_id_and_span(fs, obj_id | PSRAMFS_OBJ_ID_IX_FLAG, 0, 0, &pix);
  PSRAMFS_CHECK_RES(res);

  res = psramfs_object_open_by_page(fs, pix, fd, flags, mode);

  return res;
}

// Open object by page index
s32_t psramfs_object_open_by_page(
    psram *fs,
    psramfs_page_ix pix,
    psramfs_fd *fd,
    psramfs_flags flags,
    psramfs_mode mode) {
  (void)mode;
  s32_t res = PSRAMFS_OK;
  psramfs_page_object_ix_header oix_hdr;
  psramfs_obj_id obj_id;

  res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_READ,
      fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, pix), sizeof(psramfs_page_object_ix_header), (u8_t *)&oix_hdr);
  PSRAMFS_CHECK_RES(res);

  psramfs_block_ix bix = PSRAMFS_BLOCK_FOR_PAGE(fs, pix);
  int entry = PSRAMFS_OBJ_LOOKUP_ENTRY_FOR_PAGE(fs, pix);

  res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_READ,
      0,  PSRAMFS_BLOCK_TO_PADDR(fs, bix) + entry * sizeof(psramfs_obj_id), sizeof(psramfs_obj_id), (u8_t *)&obj_id);

  fd->fs = fs;
  fd->objix_hdr_pix = pix;
  fd->size = oix_hdr.size;
  fd->offset = 0;
  fd->cursor_objix_pix = pix;
  fd->cursor_objix_spix = 0;
  fd->obj_id = obj_id;
  fd->flags = flags;

  PSRAMFS_VALIDATE_OBJIX(oix_hdr.p_hdr, fd->obj_id, 0);

  PSRAMFS_DBG("open: fd "_SPIPRIfd" is obj id "_SPIPRIid"\n", PSRAMFS_FH_OFFS(fs, fd->file_nbr), fd->obj_id);

  return res;
}

#if !PSRAMFS_READ_ONLY
// Append to object
// keep current object index (header) page in fs->work buffer
s32_t psramfs_object_append(psramfs_fd *fd, u32_t offset, u8_t *data, u32_t len) {
  psram *fs = fd->fs;
  s32_t res = PSRAMFS_OK;
  u32_t written = 0;

  PSRAMFS_DBG("append: "_SPIPRIi" bytes @ offs "_SPIPRIi" of size "_SPIPRIi"\n", len, offset, fd->size);

  if (offset > fd->size) {
    PSRAMFS_DBG("append: offset reversed to size\n");
    offset = fd->size;
  }

  res = psramfs_gc_check(fs, len + PSRAMFS_DATA_PAGE_SIZE(fs)); // add an extra page of data worth for meta
  if (res != PSRAMFS_OK) {
    PSRAMFS_DBG("append: gc check fail "_SPIPRIi"\n", res);
  }
  PSRAMFS_CHECK_RES(res);

  psramfs_page_object_ix_header *objix_hdr = (psramfs_page_object_ix_header *)fs->work;
  psramfs_page_object_ix *objix = (psramfs_page_object_ix *)fs->work;
  psramfs_page_header p_hdr;

  psramfs_span_ix cur_objix_spix = 0;
  psramfs_span_ix prev_objix_spix = (psramfs_span_ix)-1;
  psramfs_page_ix cur_objix_pix = fd->objix_hdr_pix;
  psramfs_page_ix new_objix_hdr_page;

  psramfs_span_ix data_spix = offset / PSRAMFS_DATA_PAGE_SIZE(fs);
  psramfs_page_ix data_page;
  u32_t page_offs = offset % PSRAMFS_DATA_PAGE_SIZE(fs);

  // write all data
  while (res == PSRAMFS_OK && written < len) {
    // calculate object index page span index
    cur_objix_spix = PSRAMFS_OBJ_IX_ENTRY_SPAN_IX(fs, data_spix);

    // handle storing and loading of object indices
    if (cur_objix_spix != prev_objix_spix) {
      // new object index page
      // within this clause we return directly if something fails, object index mess-up
      if (written > 0) {
        // store previous object index page, unless first pass
        PSRAMFS_DBG("append: "_SPIPRIid" store objix "_SPIPRIpg":"_SPIPRIsp", written "_SPIPRIi"\n", fd->obj_id,
            cur_objix_pix, prev_objix_spix, written);
        if (prev_objix_spix == 0) {
          // this is an update to object index header page
          objix_hdr->size = offset+written;
          if (offset == 0) {
            // was an empty object, update same page (size was 0xffffffff)
            res = psramfs_page_index_check(fs, fd, cur_objix_pix, 0);
            PSRAMFS_CHECK_RES(res);
            res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_UPDT,
                fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, cur_objix_pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->work);
            PSRAMFS_CHECK_RES(res);
          } else {
            // was a nonempty object, update to new page
            res = psramfs_object_update_index_hdr(fs, fd, fd->obj_id,
                fd->objix_hdr_pix, fs->work, 0, 0, offset+written, &new_objix_hdr_page);
            PSRAMFS_CHECK_RES(res);
            PSRAMFS_DBG("append: "_SPIPRIid" store new objix_hdr, "_SPIPRIpg":"_SPIPRIsp", written "_SPIPRIi"\n", fd->obj_id,
                new_objix_hdr_page, 0, written);
          }
        } else {
          // this is an update to an object index page
          res = psramfs_page_index_check(fs, fd, cur_objix_pix, prev_objix_spix);
          PSRAMFS_CHECK_RES(res);

          res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_UPDT,
              fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, cur_objix_pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->work);
          PSRAMFS_CHECK_RES(res);
          psramfs_cb_object_event(fs, (psramfs_page_object_ix *)fs->work,
              PSRAMFS_EV_IX_UPD,fd->obj_id, objix->p_hdr.span_ix, cur_objix_pix, 0);
          // update length in object index header page
          res = psramfs_object_update_index_hdr(fs, fd, fd->obj_id,
              fd->objix_hdr_pix, 0, 0, 0, offset+written, &new_objix_hdr_page);
          PSRAMFS_CHECK_RES(res);
          PSRAMFS_DBG("append: "_SPIPRIid" store new size I "_SPIPRIi" in objix_hdr, "_SPIPRIpg":"_SPIPRIsp", written "_SPIPRIi"\n", fd->obj_id,
              offset+written, new_objix_hdr_page, 0, written);
        }
        fd->size = offset+written;
        fd->offset = offset+written;
      }

      // create or load new object index page
      if (cur_objix_spix == 0) {
        // load object index header page, must always exist
        PSRAMFS_DBG("append: "_SPIPRIid" load objixhdr page "_SPIPRIpg":"_SPIPRIsp"\n", fd->obj_id, cur_objix_pix, cur_objix_spix);
        res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_READ,
            fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, cur_objix_pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->work);
        PSRAMFS_CHECK_RES(res);
        PSRAMFS_VALIDATE_OBJIX(objix_hdr->p_hdr, fd->obj_id, cur_objix_spix);
      } else {
        psramfs_span_ix len_objix_spix = PSRAMFS_OBJ_IX_ENTRY_SPAN_IX(fs, (fd->size-1)/PSRAMFS_DATA_PAGE_SIZE(fs));
        // on subsequent passes, create a new object index page
        if (written > 0 || cur_objix_spix > len_objix_spix) {
          p_hdr.obj_id = fd->obj_id | PSRAMFS_OBJ_ID_IX_FLAG;
          p_hdr.span_ix = cur_objix_spix;
          p_hdr.flags = 0xff & ~(PSRAMFS_PH_FLAG_FINAL | PSRAMFS_PH_FLAG_INDEX);
          res = psramfs_page_allocate_data(fs, fd->obj_id | PSRAMFS_OBJ_ID_IX_FLAG,
              &p_hdr, 0, 0, 0, 1, &cur_objix_pix);
          PSRAMFS_CHECK_RES(res);
          // quick "load" of new object index page
          memset(fs->work, 0xff, PSRAMFS_CFG_LOG_PAGE_SZ(fs));
          _PSRAMFS_MEMCPY(fs->work, &p_hdr, sizeof(psramfs_page_header));
          psramfs_cb_object_event(fs, (psramfs_page_object_ix *)fs->work,
              PSRAMFS_EV_IX_NEW, fd->obj_id, cur_objix_spix, cur_objix_pix, 0);
          PSRAMFS_DBG("append: "_SPIPRIid" create objix page, "_SPIPRIpg":"_SPIPRIsp", written "_SPIPRIi"\n", fd->obj_id
              , cur_objix_pix, cur_objix_spix, written);
        } else {
          // on first pass, we load existing object index page
          psramfs_page_ix pix;
          PSRAMFS_DBG("append: "_SPIPRIid" find objix span_ix:"_SPIPRIsp"\n", fd->obj_id, cur_objix_spix);
          if (fd->cursor_objix_spix == cur_objix_spix) {
            pix = fd->cursor_objix_pix;
          } else {
            res = psramfs_obj_lu_find_id_and_span(fs, fd->obj_id | PSRAMFS_OBJ_ID_IX_FLAG, cur_objix_spix, 0, &pix);
            PSRAMFS_CHECK_RES(res);
          }
          PSRAMFS_DBG("append: "_SPIPRIid" found object index at page "_SPIPRIpg" [fd size "_SPIPRIi"]\n", fd->obj_id, pix, fd->size);
          res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_READ,
              fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->work);
          PSRAMFS_CHECK_RES(res);
          PSRAMFS_VALIDATE_OBJIX(objix_hdr->p_hdr, fd->obj_id, cur_objix_spix);
          cur_objix_pix = pix;
        }
        fd->cursor_objix_pix = cur_objix_pix;
        fd->cursor_objix_spix = cur_objix_spix;
        fd->offset = offset+written;
        fd->size = offset+written;
      }
      prev_objix_spix = cur_objix_spix;
    }

    // write data
    u32_t to_write = MIN(len-written, PSRAMFS_DATA_PAGE_SIZE(fs) - page_offs);
    if (page_offs == 0) {
      // at beginning of a page, allocate and write a new page of data
      p_hdr.obj_id = fd->obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG;
      p_hdr.span_ix = data_spix;
      p_hdr.flags = 0xff & ~(PSRAMFS_PH_FLAG_FINAL);  // finalize immediately
      res = psramfs_page_allocate_data(fs, fd->obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG,
          &p_hdr, &data[written], to_write, page_offs, 1, &data_page);
      PSRAMFS_DBG("append: "_SPIPRIid" store new data page, "_SPIPRIpg":"_SPIPRIsp" offset:"_SPIPRIi", len "_SPIPRIi", written "_SPIPRIi"\n", fd->obj_id,
          data_page, data_spix, page_offs, to_write, written);
    } else {
      // append to existing page, fill out free data in existing page
      if (cur_objix_spix == 0) {
        // get data page from object index header page
        data_page = ((psramfs_page_ix*)((u8_t *)objix_hdr + sizeof(psramfs_page_object_ix_header)))[data_spix];
      } else {
        // get data page from object index page
        data_page = ((psramfs_page_ix*)((u8_t *)objix + sizeof(psramfs_page_object_ix)))[PSRAMFS_OBJ_IX_ENTRY(fs, data_spix)];
      }

      res = psramfs_page_data_check(fs, fd, data_page, data_spix);
      PSRAMFS_CHECK_RES(res);

      res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_UPDT,
          fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, data_page) + sizeof(psramfs_page_header) + page_offs, to_write, &data[written]);
      PSRAMFS_DBG("append: "_SPIPRIid" store to existing data page, "_SPIPRIpg":"_SPIPRIsp" offset:"_SPIPRIi", len "_SPIPRIi", written "_SPIPRIi"\n", fd->obj_id
          , data_page, data_spix, page_offs, to_write, written);
    }

    if (res != PSRAMFS_OK) break;

    // update memory representation of object index page with new data page
    if (cur_objix_spix == 0) {
      // update object index header page
      ((psramfs_page_ix*)((u8_t *)objix_hdr + sizeof(psramfs_page_object_ix_header)))[data_spix] = data_page;
      PSRAMFS_DBG("append: "_SPIPRIid" wrote page "_SPIPRIpg" to objix_hdr entry "_SPIPRIsp" in mem\n", fd->obj_id
          , data_page, data_spix);
      objix_hdr->size = offset+written;
    } else {
      // update object index page
      ((psramfs_page_ix*)((u8_t *)objix + sizeof(psramfs_page_object_ix)))[PSRAMFS_OBJ_IX_ENTRY(fs, data_spix)] = data_page;
      PSRAMFS_DBG("append: "_SPIPRIid" wrote page "_SPIPRIpg" to objix entry "_SPIPRIsp" in mem\n", fd->obj_id
          , data_page, (psramfs_span_ix)PSRAMFS_OBJ_IX_ENTRY(fs, data_spix));
    }

    // update internals
    page_offs = 0;
    data_spix++;
    written += to_write;
  } // while all data

  fd->size = offset+written;
  fd->offset = offset+written;
  fd->cursor_objix_pix = cur_objix_pix;
  fd->cursor_objix_spix = cur_objix_spix;

  // finalize updated object indices
  s32_t res2 = PSRAMFS_OK;
  if (cur_objix_spix != 0) {
    // wrote beyond object index header page
    // write last modified object index page, unless object header index page
    PSRAMFS_DBG("append: "_SPIPRIid" store objix page, "_SPIPRIpg":"_SPIPRIsp", written "_SPIPRIi"\n", fd->obj_id,
        cur_objix_pix, cur_objix_spix, written);

    res2 = psramfs_page_index_check(fs, fd, cur_objix_pix, cur_objix_spix);
    PSRAMFS_CHECK_RES(res2);

    res2 = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_UPDT,
        fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, cur_objix_pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->work);
    PSRAMFS_CHECK_RES(res2);
    psramfs_cb_object_event(fs, (psramfs_page_object_ix *)fs->work,
        PSRAMFS_EV_IX_UPD, fd->obj_id, objix->p_hdr.span_ix, cur_objix_pix, 0);

    // update size in object header index page
    res2 = psramfs_object_update_index_hdr(fs, fd, fd->obj_id,
        fd->objix_hdr_pix, 0, 0, 0, offset+written, &new_objix_hdr_page);
    PSRAMFS_DBG("append: "_SPIPRIid" store new size II "_SPIPRIi" in objix_hdr, "_SPIPRIpg":"_SPIPRIsp", written "_SPIPRIi", res "_SPIPRIi"\n", fd->obj_id
        , offset+written, new_objix_hdr_page, 0, written, res2);
    PSRAMFS_CHECK_RES(res2);
  } else {
    // wrote within object index header page
    if (offset == 0) {
      // wrote to empty object - simply update size and write whole page
      objix_hdr->size = offset+written;
      PSRAMFS_DBG("append: "_SPIPRIid" store fresh objix_hdr page, "_SPIPRIpg":"_SPIPRIsp", written "_SPIPRIi"\n", fd->obj_id
          , cur_objix_pix, cur_objix_spix, written);

      res2 = psramfs_page_index_check(fs, fd, cur_objix_pix, cur_objix_spix);
      PSRAMFS_CHECK_RES(res2);

      res2 = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_UPDT,
          fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, cur_objix_pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->work);
      PSRAMFS_CHECK_RES(res2);
      // callback on object index update
      psramfs_cb_object_event(fs, (psramfs_page_object_ix *)fs->work,
          PSRAMFS_EV_IX_UPD_HDR, fd->obj_id, objix_hdr->p_hdr.span_ix, cur_objix_pix, objix_hdr->size);
    } else {
      // modifying object index header page, update size and make new copy
      res2 = psramfs_object_update_index_hdr(fs, fd, fd->obj_id,
          fd->objix_hdr_pix, fs->work, 0, 0, offset+written, &new_objix_hdr_page);
      PSRAMFS_DBG("append: "_SPIPRIid" store modified objix_hdr page, "_SPIPRIpg":"_SPIPRIsp", written "_SPIPRIi"\n", fd->obj_id
          , new_objix_hdr_page, 0, written);
      PSRAMFS_CHECK_RES(res2);
    }
  }

  return res;
} // psramfs_object_append
#endif // !PSRAMFS_READ_ONLY

#if !PSRAMFS_READ_ONLY
// Modify object
// keep current object index (header) page in fs->work buffer
s32_t psramfs_object_modify(psramfs_fd *fd, u32_t offset, u8_t *data, u32_t len) {
  psram *fs = fd->fs;
  s32_t res = PSRAMFS_OK;
  u32_t written = 0;

  res = psramfs_gc_check(fs, len + PSRAMFS_DATA_PAGE_SIZE(fs));
  PSRAMFS_CHECK_RES(res);

  psramfs_page_object_ix_header *objix_hdr = (psramfs_page_object_ix_header *)fs->work;
  psramfs_page_object_ix *objix = (psramfs_page_object_ix *)fs->work;
  psramfs_page_header p_hdr;

  psramfs_span_ix cur_objix_spix = 0;
  psramfs_span_ix prev_objix_spix = (psramfs_span_ix)-1;
  psramfs_page_ix cur_objix_pix = fd->objix_hdr_pix;
  psramfs_page_ix new_objix_hdr_pix;

  psramfs_span_ix data_spix = offset / PSRAMFS_DATA_PAGE_SIZE(fs);
  psramfs_page_ix data_pix;
  u32_t page_offs = offset % PSRAMFS_DATA_PAGE_SIZE(fs);


  // write all data
  while (res == PSRAMFS_OK && written < len) {
    // calculate object index page span index
    cur_objix_spix = PSRAMFS_OBJ_IX_ENTRY_SPAN_IX(fs, data_spix);

    // handle storing and loading of object indices
    if (cur_objix_spix != prev_objix_spix) {
      // new object index page
      // within this clause we return directly if something fails, object index mess-up
      if (written > 0) {
        // store previous object index (header) page, unless first pass
        if (prev_objix_spix == 0) {
          // store previous object index header page
          res = psramfs_object_update_index_hdr(fs, fd, fd->obj_id,
              fd->objix_hdr_pix, fs->work, 0, 0, 0, &new_objix_hdr_pix);
          PSRAMFS_DBG("modify: store modified objix_hdr page, "_SPIPRIpg":"_SPIPRIsp", written "_SPIPRIi"\n", new_objix_hdr_pix, 0, written);
          PSRAMFS_CHECK_RES(res);
        } else {
          // store new version of previous object index page
          psramfs_page_ix new_objix_pix;

          res = psramfs_page_index_check(fs, fd, cur_objix_pix, prev_objix_spix);
          PSRAMFS_CHECK_RES(res);

          res = psramfs_page_move(fs, fd->file_nbr, (u8_t*)objix, fd->obj_id, 0, cur_objix_pix, &new_objix_pix);
          PSRAMFS_DBG("modify: store previous modified objix page, "_SPIPRIid":"_SPIPRIsp", written "_SPIPRIi"\n", new_objix_pix, objix->p_hdr.span_ix, written);
          PSRAMFS_CHECK_RES(res);
          psramfs_cb_object_event(fs, (psramfs_page_object_ix *)objix,
              PSRAMFS_EV_IX_UPD, fd->obj_id, objix->p_hdr.span_ix, new_objix_pix, 0);
        }
      }

      // load next object index page
      if (cur_objix_spix == 0) {
        // load object index header page, must exist
        PSRAMFS_DBG("modify: load objixhdr page "_SPIPRIpg":"_SPIPRIsp"\n", cur_objix_pix, cur_objix_spix);
        res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_READ,
            fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, cur_objix_pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->work);
        PSRAMFS_CHECK_RES(res);
        PSRAMFS_VALIDATE_OBJIX(objix_hdr->p_hdr, fd->obj_id, cur_objix_spix);
      } else {
        // load existing object index page on first pass
        psramfs_page_ix pix;
        PSRAMFS_DBG("modify: find objix span_ix:"_SPIPRIsp"\n", cur_objix_spix);
        if (fd->cursor_objix_spix == cur_objix_spix) {
          pix = fd->cursor_objix_pix;
        } else {
          res = psramfs_obj_lu_find_id_and_span(fs, fd->obj_id | PSRAMFS_OBJ_ID_IX_FLAG, cur_objix_spix, 0, &pix);
          PSRAMFS_CHECK_RES(res);
        }
        PSRAMFS_DBG("modify: found object index at page "_SPIPRIpg"\n", pix);
        res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_READ,
            fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->work);
        PSRAMFS_CHECK_RES(res);
        PSRAMFS_VALIDATE_OBJIX(objix_hdr->p_hdr, fd->obj_id, cur_objix_spix);
        cur_objix_pix = pix;
      }
      fd->cursor_objix_pix = cur_objix_pix;
      fd->cursor_objix_spix = cur_objix_spix;
      fd->offset = offset+written;
      prev_objix_spix = cur_objix_spix;
    }

    // write partial data
    u32_t to_write = MIN(len-written, PSRAMFS_DATA_PAGE_SIZE(fs) - page_offs);
    psramfs_page_ix orig_data_pix;
    if (cur_objix_spix == 0) {
      // get data page from object index header page
      orig_data_pix = ((psramfs_page_ix*)((u8_t *)objix_hdr + sizeof(psramfs_page_object_ix_header)))[data_spix];
    } else {
      // get data page from object index page
      orig_data_pix = ((psramfs_page_ix*)((u8_t *)objix + sizeof(psramfs_page_object_ix)))[PSRAMFS_OBJ_IX_ENTRY(fs, data_spix)];
    }

    p_hdr.obj_id = fd->obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG;
    p_hdr.span_ix = data_spix;
    p_hdr.flags = 0xff;
    if (page_offs == 0 && to_write == PSRAMFS_DATA_PAGE_SIZE(fs)) {
      // a full page, allocate and write a new page of data
      res = psramfs_page_allocate_data(fs, fd->obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG,
          &p_hdr, &data[written], to_write, page_offs, 1, &data_pix);
      PSRAMFS_DBG("modify: store new data page, "_SPIPRIpg":"_SPIPRIsp" offset:"_SPIPRIi", len "_SPIPRIi", written "_SPIPRIi"\n", data_pix, data_spix, page_offs, to_write, written);
    } else {
      // write to existing page, allocate new and copy unmodified data

      res = psramfs_page_data_check(fs, fd, orig_data_pix, data_spix);
      PSRAMFS_CHECK_RES(res);

      res = psramfs_page_allocate_data(fs, fd->obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG,
          &p_hdr, 0, 0, 0, 0, &data_pix);
      if (res != PSRAMFS_OK) break;

      // copy unmodified data
      if (page_offs > 0) {
        // before modification
        res = psramfs_phys_cpy(fs, fd->file_nbr,
            PSRAMFS_PAGE_TO_PADDR(fs, data_pix) + sizeof(psramfs_page_header),
            PSRAMFS_PAGE_TO_PADDR(fs, orig_data_pix) + sizeof(psramfs_page_header),
            page_offs);
        if (res != PSRAMFS_OK) break;
      }
      if (page_offs + to_write < PSRAMFS_DATA_PAGE_SIZE(fs)) {
        // after modification
        res = psramfs_phys_cpy(fs, fd->file_nbr,
            PSRAMFS_PAGE_TO_PADDR(fs, data_pix) + sizeof(psramfs_page_header) + page_offs + to_write,
            PSRAMFS_PAGE_TO_PADDR(fs, orig_data_pix) + sizeof(psramfs_page_header) + page_offs + to_write,
            PSRAMFS_DATA_PAGE_SIZE(fs) - (page_offs + to_write));
        if (res != PSRAMFS_OK) break;
      }

      res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_UPDT,
          fd->file_nbr,
          PSRAMFS_PAGE_TO_PADDR(fs, data_pix) + sizeof(psramfs_page_header) + page_offs, to_write, &data[written]);
      if (res != PSRAMFS_OK) break;
      p_hdr.flags &= ~PSRAMFS_PH_FLAG_FINAL;
      res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_UPDT,
          fd->file_nbr,
          PSRAMFS_PAGE_TO_PADDR(fs, data_pix) + offsetof(psramfs_page_header, flags),
          sizeof(u8_t),
          (u8_t *)&p_hdr.flags);
      if (res != PSRAMFS_OK) break;

      PSRAMFS_DBG("modify: store to existing data page, src:"_SPIPRIpg", dst:"_SPIPRIpg":"_SPIPRIsp" offset:"_SPIPRIi", len "_SPIPRIi", written "_SPIPRIi"\n", orig_data_pix, data_pix, data_spix, page_offs, to_write, written);
    }

    // delete original data page
    res = psramfs_page_delete(fs, orig_data_pix);
    if (res != PSRAMFS_OK) break;
    // update memory representation of object index page with new data page
    if (cur_objix_spix == 0) {
      // update object index header page
      ((psramfs_page_ix*)((u8_t *)objix_hdr + sizeof(psramfs_page_object_ix_header)))[data_spix] = data_pix;
      PSRAMFS_DBG("modify: wrote page "_SPIPRIpg" to objix_hdr entry "_SPIPRIsp" in mem\n", data_pix, data_spix);
    } else {
      // update object index page
      ((psramfs_page_ix*)((u8_t *)objix + sizeof(psramfs_page_object_ix)))[PSRAMFS_OBJ_IX_ENTRY(fs, data_spix)] = data_pix;
      PSRAMFS_DBG("modify: wrote page "_SPIPRIpg" to objix entry "_SPIPRIsp" in mem\n", data_pix, (psramfs_span_ix)PSRAMFS_OBJ_IX_ENTRY(fs, data_spix));
    }

    // update internals
    page_offs = 0;
    data_spix++;
    written += to_write;
  } // while all data

  fd->offset = offset+written;
  fd->cursor_objix_pix = cur_objix_pix;
  fd->cursor_objix_spix = cur_objix_spix;

  // finalize updated object indices
  s32_t res2 = PSRAMFS_OK;
  if (cur_objix_spix != 0) {
    // wrote beyond object index header page
    // write last modified object index page
    // move and update page
    psramfs_page_ix new_objix_pix;

    res2 = psramfs_page_index_check(fs, fd, cur_objix_pix, cur_objix_spix);
    PSRAMFS_CHECK_RES(res2);

    res2 = psramfs_page_move(fs, fd->file_nbr, (u8_t*)objix, fd->obj_id, 0, cur_objix_pix, &new_objix_pix);
    PSRAMFS_DBG("modify: store modified objix page, "_SPIPRIpg":"_SPIPRIsp", written "_SPIPRIi"\n", new_objix_pix, cur_objix_spix, written);
    fd->cursor_objix_pix = new_objix_pix;
    fd->cursor_objix_spix = cur_objix_spix;
    PSRAMFS_CHECK_RES(res2);
    psramfs_cb_object_event(fs, (psramfs_page_object_ix *)objix,
        PSRAMFS_EV_IX_UPD, fd->obj_id, objix->p_hdr.span_ix, new_objix_pix, 0);

  } else {
    // wrote within object index header page
    res2 = psramfs_object_update_index_hdr(fs, fd, fd->obj_id,
        fd->objix_hdr_pix, fs->work, 0, 0, 0, &new_objix_hdr_pix);
    PSRAMFS_DBG("modify: store modified objix_hdr page, "_SPIPRIpg":"_SPIPRIsp", written "_SPIPRIi"\n", new_objix_hdr_pix, 0, written);
    PSRAMFS_CHECK_RES(res2);
  }

  return res;
} // psramfs_object_modify
#endif // !PSRAMFS_READ_ONLY

static s32_t psramfs_object_find_object_index_header_by_name_v(
    psram *fs,
    psramfs_obj_id obj_id,
    psramfs_block_ix bix,
    int ix_entry,
    const void *user_const_p,
    void *user_var_p) {
  (void)user_var_p;
  s32_t res;
  psramfs_page_object_ix_header objix_hdr;
  psramfs_page_ix pix = PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, ix_entry);
  if (obj_id == PSRAMFS_OBJ_ID_FREE || obj_id == PSRAMFS_OBJ_ID_DELETED ||
      (obj_id & PSRAMFS_OBJ_ID_IX_FLAG) == 0) {
    return PSRAMFS_VIS_COUNTINUE;
  }
  res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
      0, PSRAMFS_PAGE_TO_PADDR(fs, pix), sizeof(psramfs_page_object_ix_header), (u8_t *)&objix_hdr);
  PSRAMFS_CHECK_RES(res);
  if (objix_hdr.p_hdr.span_ix == 0 &&
      (objix_hdr.p_hdr.flags & (PSRAMFS_PH_FLAG_DELET | PSRAMFS_PH_FLAG_FINAL | PSRAMFS_PH_FLAG_IXDELE)) ==
          (PSRAMFS_PH_FLAG_DELET | PSRAMFS_PH_FLAG_IXDELE)) {
    if (strcmp((const char*)user_const_p, (char*)objix_hdr.name) == 0) {
      return PSRAMFS_OK;
    }
  }

  return PSRAMFS_VIS_COUNTINUE;
}

// Finds object index header page by name
s32_t psramfs_object_find_object_index_header_by_name(
    psram *fs,
    const u8_t name[PSRAMFS_OBJ_NAME_LEN],
    psramfs_page_ix *pix) {
  s32_t res;
  psramfs_block_ix bix;
  int entry;

  res = psramfs_obj_lu_find_entry_visitor(fs,
      fs->cursor_block_ix,
      fs->cursor_obj_lu_entry,
      0,
      0,
      psramfs_object_find_object_index_header_by_name_v,
      name,
      0,
      &bix,
      &entry);

  if (res == PSRAMFS_VIS_END) {
    res = PSRAMFS_ERR_NOT_FOUND;
  }
  PSRAMFS_CHECK_RES(res);

  if (pix) {
    *pix = PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, entry);
  }

  fs->cursor_block_ix = bix;
  fs->cursor_obj_lu_entry = entry;

  return res;
}

#if !PSRAMFS_READ_ONLY
// Truncates object to new size. If new size is null, object may be removed totally
s32_t psramfs_object_truncate(
    psramfs_fd *fd,
    u32_t new_size,
    u8_t remove_full) {
  s32_t res = PSRAMFS_OK;
  psram *fs = fd->fs;

  if ((fd->size == PSRAMFS_UNDEFINED_LEN || fd->size == 0) && !remove_full) {
    // no op
    return res;
  }

  // need 2 pages if not removing: object index page + possibly chopped data page
  if (remove_full == 0) {
    res = psramfs_gc_check(fs, PSRAMFS_DATA_PAGE_SIZE(fs) * 2);
    PSRAMFS_CHECK_RES(res);
  }

  psramfs_page_ix objix_pix = fd->objix_hdr_pix;
  psramfs_span_ix data_spix = (fd->size > 0 ? fd->size-1 : 0) / PSRAMFS_DATA_PAGE_SIZE(fs);
  u32_t cur_size = fd->size == (u32_t)PSRAMFS_UNDEFINED_LEN ? 0 : fd->size ;
  psramfs_span_ix cur_objix_spix = 0;
  psramfs_span_ix prev_objix_spix = (psramfs_span_ix)-1;
  psramfs_page_object_ix_header *objix_hdr = (psramfs_page_object_ix_header *)fs->work;
  psramfs_page_object_ix *objix = (psramfs_page_object_ix *)fs->work;
  psramfs_page_ix data_pix;
  psramfs_page_ix new_objix_hdr_pix;

  // before truncating, check if object is to be fully removed and mark this
  if (remove_full && new_size == 0) {
    u8_t flags = ~( PSRAMFS_PH_FLAG_USED | PSRAMFS_PH_FLAG_INDEX | PSRAMFS_PH_FLAG_FINAL | PSRAMFS_PH_FLAG_IXDELE);
    res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_UPDT,
        fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, fd->objix_hdr_pix) + offsetof(psramfs_page_header, flags),
        sizeof(u8_t),
        (u8_t *)&flags);
    PSRAMFS_CHECK_RES(res);
  }

  // delete from end of object until desired len is reached
  while (cur_size > new_size) {
    cur_objix_spix = PSRAMFS_OBJ_IX_ENTRY_SPAN_IX(fs, data_spix);

    // put object index for current data span index in work buffer
    if (prev_objix_spix != cur_objix_spix) {
      if (prev_objix_spix != (psramfs_span_ix)-1) {
        // remove previous object index page
        PSRAMFS_DBG("truncate: delete objix page "_SPIPRIpg":"_SPIPRIsp"\n", objix_pix, prev_objix_spix);

        res = psramfs_page_index_check(fs, fd, objix_pix, prev_objix_spix);
        PSRAMFS_CHECK_RES(res);

        res = psramfs_page_delete(fs, objix_pix);
        PSRAMFS_CHECK_RES(res);
        psramfs_cb_object_event(fs, (psramfs_page_object_ix *)0,
            PSRAMFS_EV_IX_DEL, fd->obj_id, objix->p_hdr.span_ix, objix_pix, 0);
        if (prev_objix_spix > 0) {
          // Update object index header page, unless we totally want to remove the file.
          // If fully removing, we're not keeping consistency as good as when storing the header between chunks,
          // would we be aborted. But when removing full files, a crammed system may otherwise
          // report ERR_FULL a la windows. We cannot have that.
          // Hence, take the risk - if aborted, a file check would free the lost pages and mend things
          // as the file is marked as fully deleted in the beginning.
          if (remove_full == 0) {
            PSRAMFS_DBG("truncate: update objix hdr page "_SPIPRIpg":"_SPIPRIsp" to size "_SPIPRIi"\n", fd->objix_hdr_pix, prev_objix_spix, cur_size);
            res = psramfs_object_update_index_hdr(fs, fd, fd->obj_id,
                fd->objix_hdr_pix, 0, 0, 0, cur_size, &new_objix_hdr_pix);
            PSRAMFS_CHECK_RES(res);
          }
          fd->size = cur_size;
        }
      }
      // load current object index (header) page
      if (cur_objix_spix == 0) {
        objix_pix = fd->objix_hdr_pix;
      } else {
        res = psramfs_obj_lu_find_id_and_span(fs, fd->obj_id | PSRAMFS_OBJ_ID_IX_FLAG, cur_objix_spix, 0, &objix_pix);
        PSRAMFS_CHECK_RES(res);
      }

      PSRAMFS_DBG("truncate: load objix page "_SPIPRIpg":"_SPIPRIsp" for data spix:"_SPIPRIsp"\n", objix_pix, cur_objix_spix, data_spix);
      res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_READ,
          fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, objix_pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->work);
      PSRAMFS_CHECK_RES(res);
      PSRAMFS_VALIDATE_OBJIX(objix_hdr->p_hdr, fd->obj_id, cur_objix_spix);
      fd->cursor_objix_pix = objix_pix;
      fd->cursor_objix_spix = cur_objix_spix;
      fd->offset = cur_size;

      prev_objix_spix = cur_objix_spix;
    }

    if (cur_objix_spix == 0) {
      // get data page from object index header page
      data_pix = ((psramfs_page_ix*)((u8_t *)objix_hdr + sizeof(psramfs_page_object_ix_header)))[data_spix];
      ((psramfs_page_ix*)((u8_t *)objix_hdr + sizeof(psramfs_page_object_ix_header)))[data_spix] = PSRAMFS_OBJ_ID_FREE;
    } else {
      // get data page from object index page
      data_pix = ((psramfs_page_ix*)((u8_t *)objix + sizeof(psramfs_page_object_ix)))[PSRAMFS_OBJ_IX_ENTRY(fs, data_spix)];
      ((psramfs_page_ix*)((u8_t *)objix + sizeof(psramfs_page_object_ix)))[PSRAMFS_OBJ_IX_ENTRY(fs, data_spix)] = PSRAMFS_OBJ_ID_FREE;
    }

    PSRAMFS_DBG("truncate: got data pix "_SPIPRIpg"\n", data_pix);

    if (new_size == 0 || remove_full || cur_size - new_size >= PSRAMFS_DATA_PAGE_SIZE(fs)) {
      // delete full data page
      res = psramfs_page_data_check(fs, fd, data_pix, data_spix);
      if (res != PSRAMFS_ERR_DELETED && res != PSRAMFS_OK && res != PSRAMFS_ERR_INDEX_REF_FREE) {
        PSRAMFS_DBG("truncate: err validating data pix "_SPIPRIi"\n", res);
        break;
      }

      if (res == PSRAMFS_OK) {
        res = psramfs_page_delete(fs, data_pix);
        if (res != PSRAMFS_OK) {
          PSRAMFS_DBG("truncate: err deleting data pix "_SPIPRIi"\n", res);
          break;
        }
      } else if (res == PSRAMFS_ERR_DELETED || res == PSRAMFS_ERR_INDEX_REF_FREE) {
        res = PSRAMFS_OK;
      }

      // update current size
      if (cur_size % PSRAMFS_DATA_PAGE_SIZE(fs) == 0) {
        cur_size -= PSRAMFS_DATA_PAGE_SIZE(fs);
      } else {
        cur_size -= cur_size % PSRAMFS_DATA_PAGE_SIZE(fs);
      }
      fd->size = cur_size;
      fd->offset = cur_size;
      PSRAMFS_DBG("truncate: delete data page "_SPIPRIpg" for data spix:"_SPIPRIsp", cur_size:"_SPIPRIi"\n", data_pix, data_spix, cur_size);
    } else {
      // delete last page, partially
      psramfs_page_header p_hdr;
      psramfs_page_ix new_data_pix;
      u32_t bytes_to_remove = PSRAMFS_DATA_PAGE_SIZE(fs) - (new_size % PSRAMFS_DATA_PAGE_SIZE(fs));
      PSRAMFS_DBG("truncate: delete "_SPIPRIi" bytes from data page "_SPIPRIpg" for data spix:"_SPIPRIsp", cur_size:"_SPIPRIi"\n", bytes_to_remove, data_pix, data_spix, cur_size);

      res = psramfs_page_data_check(fs, fd, data_pix, data_spix);
      if (res != PSRAMFS_OK) break;

      p_hdr.obj_id = fd->obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG;
      p_hdr.span_ix = data_spix;
      p_hdr.flags = 0xff;
      // allocate new page and copy unmodified data
      res = psramfs_page_allocate_data(fs, fd->obj_id & ~PSRAMFS_OBJ_ID_IX_FLAG,
          &p_hdr, 0, 0, 0, 0, &new_data_pix);
      if (res != PSRAMFS_OK) break;
      res = psramfs_phys_cpy(fs, 0,
          PSRAMFS_PAGE_TO_PADDR(fs, new_data_pix) + sizeof(psramfs_page_header),
          PSRAMFS_PAGE_TO_PADDR(fs, data_pix) + sizeof(psramfs_page_header),
          PSRAMFS_DATA_PAGE_SIZE(fs) - bytes_to_remove);
      if (res != PSRAMFS_OK) break;
      // delete original data page
      res = psramfs_page_delete(fs, data_pix);
      if (res != PSRAMFS_OK) break;
      p_hdr.flags &= ~PSRAMFS_PH_FLAG_FINAL;
      res = _psramfs_wr(fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_UPDT,
          fd->file_nbr,
          PSRAMFS_PAGE_TO_PADDR(fs, new_data_pix) + offsetof(psramfs_page_header, flags),
          sizeof(u8_t),
          (u8_t *)&p_hdr.flags);
      if (res != PSRAMFS_OK) break;

      // update memory representation of object index page with new data page
      if (cur_objix_spix == 0) {
        // update object index header page
        ((psramfs_page_ix*)((u8_t *)objix_hdr + sizeof(psramfs_page_object_ix_header)))[data_spix] = new_data_pix;
        PSRAMFS_DBG("truncate: wrote page "_SPIPRIpg" to objix_hdr entry "_SPIPRIsp" in mem\n", new_data_pix, (psramfs_span_ix)PSRAMFS_OBJ_IX_ENTRY(fs, data_spix));
      } else {
        // update object index page
        ((psramfs_page_ix*)((u8_t *)objix + sizeof(psramfs_page_object_ix)))[PSRAMFS_OBJ_IX_ENTRY(fs, data_spix)] = new_data_pix;
        PSRAMFS_DBG("truncate: wrote page "_SPIPRIpg" to objix entry "_SPIPRIsp" in mem\n", new_data_pix, (psramfs_span_ix)PSRAMFS_OBJ_IX_ENTRY(fs, data_spix));
      }
      cur_size = new_size;
      fd->size = new_size;
      fd->offset = cur_size;
      break;
    }
    data_spix--;
  } // while all data

  // update object indices
  if (cur_objix_spix == 0) {
    // update object index header page
    if (cur_size == 0) {
      if (remove_full) {
        // remove object altogether
        PSRAMFS_DBG("truncate: remove object index header page "_SPIPRIpg"\n", objix_pix);

        res = psramfs_page_index_check(fs, fd, objix_pix, 0);
        PSRAMFS_CHECK_RES(res);

        res = psramfs_page_delete(fs, objix_pix);
        PSRAMFS_CHECK_RES(res);
        psramfs_cb_object_event(fs, (psramfs_page_object_ix *)0,
            PSRAMFS_EV_IX_DEL, fd->obj_id, 0, objix_pix, 0);
      } else {
        // make uninitialized object
        PSRAMFS_DBG("truncate: reset objix_hdr page "_SPIPRIpg"\n", objix_pix);
        memset(fs->work + sizeof(psramfs_page_object_ix_header), 0xff,
            PSRAMFS_CFG_LOG_PAGE_SZ(fs) - sizeof(psramfs_page_object_ix_header));
        res = psramfs_object_update_index_hdr(fs, fd, fd->obj_id,
            objix_pix, fs->work, 0, 0, PSRAMFS_UNDEFINED_LEN, &new_objix_hdr_pix);
        PSRAMFS_CHECK_RES(res);
      }
    } else {
      // update object index header page
      PSRAMFS_DBG("truncate: update object index header page with indices and size\n");
      res = psramfs_object_update_index_hdr(fs, fd, fd->obj_id,
          objix_pix, fs->work, 0, 0, cur_size, &new_objix_hdr_pix);
      PSRAMFS_CHECK_RES(res);
    }
  } else {
    // update both current object index page and object index header page
    psramfs_page_ix new_objix_pix;

    res = psramfs_page_index_check(fs, fd, objix_pix, cur_objix_spix);
    PSRAMFS_CHECK_RES(res);

    // move and update object index page
    res = psramfs_page_move(fs, fd->file_nbr, (u8_t*)objix_hdr, fd->obj_id, 0, objix_pix, &new_objix_pix);
    PSRAMFS_CHECK_RES(res);
    psramfs_cb_object_event(fs, (psramfs_page_object_ix *)objix_hdr,
        PSRAMFS_EV_IX_UPD, fd->obj_id, objix->p_hdr.span_ix, new_objix_pix, 0);
    PSRAMFS_DBG("truncate: store modified objix page, "_SPIPRIpg":"_SPIPRIsp"\n", new_objix_pix, cur_objix_spix);
    fd->cursor_objix_pix = new_objix_pix;
    fd->cursor_objix_spix = cur_objix_spix;
    fd->offset = cur_size;
    // update object index header page with new size
    res = psramfs_object_update_index_hdr(fs, fd, fd->obj_id,
        fd->objix_hdr_pix, 0, 0, 0, cur_size, &new_objix_hdr_pix);
    PSRAMFS_CHECK_RES(res);
  }
  fd->size = cur_size;

  return res;
} // psramfs_object_truncate
#endif // !PSRAMFS_READ_ONLY

s32_t psramfs_object_read(
    psramfs_fd *fd,
    u32_t offset,
    u32_t len,
    u8_t *dst) {
  s32_t res = PSRAMFS_OK;
  psram *fs = fd->fs;
  psramfs_page_ix objix_pix;
  psramfs_page_ix data_pix;
  psramfs_span_ix data_spix = offset / PSRAMFS_DATA_PAGE_SIZE(fs);
  u32_t cur_offset = offset;
  psramfs_span_ix cur_objix_spix;
  psramfs_span_ix prev_objix_spix = (psramfs_span_ix)-1;
  psramfs_page_object_ix_header *objix_hdr = (psramfs_page_object_ix_header *)fs->work;
  psramfs_page_object_ix *objix = (psramfs_page_object_ix *)fs->work;

  while (cur_offset < offset + len) {
#if PSRAMFS_IX_MAP
    // check if we have a memory, index map and if so, if we're within index map's range
    // and if so, if the entry is populated
    if (fd->ix_map && data_spix >= fd->ix_map->start_spix && data_spix <= fd->ix_map->end_spix
        && fd->ix_map->map_buf[data_spix - fd->ix_map->start_spix]) {
      data_pix = fd->ix_map->map_buf[data_spix - fd->ix_map->start_spix];
    } else {
#endif
      cur_objix_spix = PSRAMFS_OBJ_IX_ENTRY_SPAN_IX(fs, data_spix);
      if (prev_objix_spix != cur_objix_spix) {
        // load current object index (header) page
        if (cur_objix_spix == 0) {
          objix_pix = fd->objix_hdr_pix;
        } else {
          PSRAMFS_DBG("read: find objix "_SPIPRIid":"_SPIPRIsp"\n", fd->obj_id, cur_objix_spix);
          if (fd->cursor_objix_spix == cur_objix_spix) {
            objix_pix = fd->cursor_objix_pix;
          } else {
            res = psramfs_obj_lu_find_id_and_span(fs, fd->obj_id | PSRAMFS_OBJ_ID_IX_FLAG, cur_objix_spix, 0, &objix_pix);
            PSRAMFS_CHECK_RES(res);
          }
        }
        PSRAMFS_DBG("read: load objix page "_SPIPRIpg":"_SPIPRIsp" for data spix:"_SPIPRIsp"\n", objix_pix, cur_objix_spix, data_spix);
        res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_IX | PSRAMFS_OP_C_READ,
            fd->file_nbr, PSRAMFS_PAGE_TO_PADDR(fs, objix_pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->work);
        PSRAMFS_CHECK_RES(res);
        PSRAMFS_VALIDATE_OBJIX(objix->p_hdr, fd->obj_id, cur_objix_spix);

        fd->offset = cur_offset;
        fd->cursor_objix_pix = objix_pix;
        fd->cursor_objix_spix = cur_objix_spix;

        prev_objix_spix = cur_objix_spix;
      }

      if (cur_objix_spix == 0) {
        // get data page from object index header page
        data_pix = ((psramfs_page_ix*)((u8_t *)objix_hdr + sizeof(psramfs_page_object_ix_header)))[data_spix];
      } else {
        // get data page from object index page
        data_pix = ((psramfs_page_ix*)((u8_t *)objix + sizeof(psramfs_page_object_ix)))[PSRAMFS_OBJ_IX_ENTRY(fs, data_spix)];
      }
#if PSRAMFS_IX_MAP
    }
#endif
    // all remaining data
    u32_t len_to_read = offset + len - cur_offset;
    // remaining data in page
    len_to_read = MIN(len_to_read, PSRAMFS_DATA_PAGE_SIZE(fs) - (cur_offset % PSRAMFS_DATA_PAGE_SIZE(fs)));
    // remaining data in file
    len_to_read = MIN(len_to_read, fd->size - cur_offset);
    PSRAMFS_DBG("read: offset:"_SPIPRIi" rd:"_SPIPRIi" data spix:"_SPIPRIsp" is data_pix:"_SPIPRIpg" addr:"_SPIPRIad"\n", cur_offset, len_to_read, data_spix, data_pix,
        (u32_t)(PSRAMFS_PAGE_TO_PADDR(fs, data_pix) + sizeof(psramfs_page_header) + (cur_offset % PSRAMFS_DATA_PAGE_SIZE(fs))));
    if (len_to_read <= 0) {
      res = PSRAMFS_ERR_END_OF_OBJECT;
      break;
    }
    res = psramfs_page_data_check(fs, fd, data_pix, data_spix);
    PSRAMFS_CHECK_RES(res);
    res = _psramfs_rd(
        fs, PSRAMFS_OP_T_OBJ_DA | PSRAMFS_OP_C_READ,
        fd->file_nbr,
        PSRAMFS_PAGE_TO_PADDR(fs, data_pix) + sizeof(psramfs_page_header) + (cur_offset % PSRAMFS_DATA_PAGE_SIZE(fs)),
        len_to_read,
        dst);
    PSRAMFS_CHECK_RES(res);
    dst += len_to_read;
    cur_offset += len_to_read;
    fd->offset = cur_offset;
    data_spix++;
  }

  return res;
}

#if !PSRAMFS_READ_ONLY
typedef struct {
  psramfs_obj_id min_obj_id;
  psramfs_obj_id max_obj_id;
  u32_t compaction;
  const u8_t *conflicting_name;
} psramfs_free_obj_id_state;

static s32_t psramfs_obj_lu_find_free_obj_id_bitmap_v(psram *fs, psramfs_obj_id id, psramfs_block_ix bix, int ix_entry,
    const void *user_const_p, void *user_var_p) {
  if (id != PSRAMFS_OBJ_ID_FREE && id != PSRAMFS_OBJ_ID_DELETED) {
    psramfs_obj_id min_obj_id = *((psramfs_obj_id*)user_var_p);
    const u8_t *conflicting_name = (const u8_t*)user_const_p;

    // if conflicting name parameter is given, also check if this name is found in object index hdrs
    if (conflicting_name && (id & PSRAMFS_OBJ_ID_IX_FLAG)) {
      psramfs_page_ix pix = PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, ix_entry);
      int res;
      psramfs_page_object_ix_header objix_hdr;
      res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
          0, PSRAMFS_PAGE_TO_PADDR(fs, pix), sizeof(psramfs_page_object_ix_header), (u8_t *)&objix_hdr);
      PSRAMFS_CHECK_RES(res);
      if (objix_hdr.p_hdr.span_ix == 0 &&
          (objix_hdr.p_hdr.flags & (PSRAMFS_PH_FLAG_DELET | PSRAMFS_PH_FLAG_FINAL | PSRAMFS_PH_FLAG_IXDELE)) ==
              (PSRAMFS_PH_FLAG_DELET | PSRAMFS_PH_FLAG_IXDELE)) {
        if (strcmp((const char*)user_const_p, (char*)objix_hdr.name) == 0) {
          return PSRAMFS_ERR_CONFLICTING_NAME;
        }
      }
    }

    id &= ~PSRAMFS_OBJ_ID_IX_FLAG;
    u32_t bit_ix = (id-min_obj_id) & 7;
    int byte_ix = (id-min_obj_id) >> 3;
    if (byte_ix >= 0 && (u32_t)byte_ix < PSRAMFS_CFG_LOG_PAGE_SZ(fs)) {
      fs->work[byte_ix] |= (1<<bit_ix);
    }
  }
  return PSRAMFS_VIS_COUNTINUE;
}

static s32_t psramfs_obj_lu_find_free_obj_id_compact_v(psram *fs, psramfs_obj_id id, psramfs_block_ix bix, int ix_entry,
    const void *user_const_p, void *user_var_p) {
  (void)user_var_p;
  if (id != PSRAMFS_OBJ_ID_FREE && id != PSRAMFS_OBJ_ID_DELETED && (id & PSRAMFS_OBJ_ID_IX_FLAG)) {
    s32_t res;
    const psramfs_free_obj_id_state *state = (const psramfs_free_obj_id_state*)user_const_p;
    psramfs_page_object_ix_header objix_hdr;

    res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
        0, PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PADDR(fs, bix, ix_entry), sizeof(psramfs_page_object_ix_header), (u8_t*)&objix_hdr);
    if (res == PSRAMFS_OK && objix_hdr.p_hdr.span_ix == 0 &&
        ((objix_hdr.p_hdr.flags & (PSRAMFS_PH_FLAG_INDEX | PSRAMFS_PH_FLAG_FINAL | PSRAMFS_PH_FLAG_DELET)) ==
            (PSRAMFS_PH_FLAG_DELET))) {
      // ok object look up entry
      if (state->conflicting_name && strcmp((const char *)state->conflicting_name, (char *)objix_hdr.name) == 0) {
        return PSRAMFS_ERR_CONFLICTING_NAME;
      }

      id &= ~PSRAMFS_OBJ_ID_IX_FLAG;
      if (id >= state->min_obj_id && id <= state->max_obj_id) {
        u8_t *map = (u8_t *)fs->work;
        int ix = (id - state->min_obj_id) / state->compaction;
        //PSRAMFS_DBG("free_obj_id: add ix "_SPIPRIi" for id "_SPIPRIid" min"_SPIPRIid" max"_SPIPRIid" comp:"_SPIPRIi"\n", ix, id, state->min_obj_id, state->max_obj_id, state->compaction);
        map[ix]++;
      }
    }
  }
  return PSRAMFS_VIS_COUNTINUE;
}

// Scans thru all object lookup for object index header pages. If total possible number of
// object ids cannot fit into a work buffer, these are grouped. When a group containing free
// object ids is found, the object lu is again scanned for object ids within group and bitmasked.
// Finally, the bitmask is searched for a free id
s32_t psramfs_obj_lu_find_free_obj_id(psram *fs, psramfs_obj_id *obj_id, const u8_t *conflicting_name) {
  s32_t res = PSRAMFS_OK;
  u32_t max_objects = (fs->block_count * PSRAMFS_OBJ_LOOKUP_MAX_ENTRIES(fs)) / 2;
  psramfs_free_obj_id_state state;
  psramfs_obj_id free_obj_id = PSRAMFS_OBJ_ID_FREE;
  state.min_obj_id = 1;
  state.max_obj_id = max_objects + 1;
  if (state.max_obj_id & PSRAMFS_OBJ_ID_IX_FLAG) {
    state.max_obj_id = ((psramfs_obj_id)-1) & ~PSRAMFS_OBJ_ID_IX_FLAG;
  }
  state.compaction = 0;
  state.conflicting_name = conflicting_name;
  while (res == PSRAMFS_OK && free_obj_id == PSRAMFS_OBJ_ID_FREE) {
    if (state.max_obj_id - state.min_obj_id <= (psramfs_obj_id)PSRAMFS_CFG_LOG_PAGE_SZ(fs)*8) {
      // possible to represent in bitmap
      u32_t i, j;
      PSRAMFS_DBG("free_obj_id: BITM min:"_SPIPRIid" max:"_SPIPRIid"\n", state.min_obj_id, state.max_obj_id);

      memset(fs->work, 0, PSRAMFS_CFG_LOG_PAGE_SZ(fs));
      res = psramfs_obj_lu_find_entry_visitor(fs, 0, 0, 0, 0, psramfs_obj_lu_find_free_obj_id_bitmap_v,
          conflicting_name, &state.min_obj_id, 0, 0);
      if (res == PSRAMFS_VIS_END) res = PSRAMFS_OK;
      PSRAMFS_CHECK_RES(res);
      // traverse bitmask until found free obj_id
      for (i = 0; i < PSRAMFS_CFG_LOG_PAGE_SZ(fs); i++) {
        u8_t mask = fs->work[i];
        if (mask == 0xff) {
          continue;
        }
        for (j = 0; j < 8; j++) {
          if ((mask & (1<<j)) == 0) {
            *obj_id = (i<<3)+j+state.min_obj_id;
            return PSRAMFS_OK;
          }
        }
      }
      return PSRAMFS_ERR_FULL;
    } else {
      // not possible to represent all ids in range in a bitmap, compact and count
      if (state.compaction != 0) {
        // select element in compacted table, decrease range and recompact
        u32_t i, min_i = 0;
        u8_t *map = (u8_t *)fs->work;
        u8_t min_count = 0xff;

        for (i = 0; i < PSRAMFS_CFG_LOG_PAGE_SZ(fs)/sizeof(u8_t); i++) {
          if (map[i] < min_count) {
            min_count = map[i];
            min_i = i;
            if (min_count == 0) {
              break;
            }
          }
        }

        if (min_count == state.compaction) {
          // there are no free objids!
          PSRAMFS_DBG("free_obj_id: compacted table is full\n");
          return PSRAMFS_ERR_FULL;
        }

        PSRAMFS_DBG("free_obj_id: COMP select index:"_SPIPRIi" min_count:"_SPIPRIi" min:"_SPIPRIid" max:"_SPIPRIid" compact:"_SPIPRIi"\n", min_i, min_count, state.min_obj_id, state.max_obj_id, state.compaction);

        if (min_count == 0) {
          // no id in this range, skip compacting and use directly
          *obj_id = min_i * state.compaction + state.min_obj_id;
          return PSRAMFS_OK;
        } else {
          PSRAMFS_DBG("free_obj_id: COMP SEL chunk:"_SPIPRIi" min:"_SPIPRIid" -> "_SPIPRIid"\n", state.compaction, state.min_obj_id, state.min_obj_id + min_i *  state.compaction);
          state.min_obj_id += min_i *  state.compaction;
          state.max_obj_id = state.min_obj_id + state.compaction;
          // decrease compaction
        }
        if ((state.max_obj_id - state.min_obj_id <= (psramfs_obj_id)PSRAMFS_CFG_LOG_PAGE_SZ(fs)*8)) {
          // no need for compacting, use bitmap
          continue;
        }
      }
      // in a work memory of log_page_size bytes, we may fit in log_page_size ids
      // todo what if compaction is > 255 - then we cannot fit it in a byte
      state.compaction = (state.max_obj_id-state.min_obj_id) / ((PSRAMFS_CFG_LOG_PAGE_SZ(fs) / sizeof(u8_t)));
      PSRAMFS_DBG("free_obj_id: COMP min:"_SPIPRIid" max:"_SPIPRIid" compact:"_SPIPRIi"\n", state.min_obj_id, state.max_obj_id, state.compaction);

      memset(fs->work, 0, PSRAMFS_CFG_LOG_PAGE_SZ(fs));
      res = psramfs_obj_lu_find_entry_visitor(fs, 0, 0, 0, 0, psramfs_obj_lu_find_free_obj_id_compact_v, &state, 0, 0, 0);
      if (res == PSRAMFS_VIS_END) res = PSRAMFS_OK;
      PSRAMFS_CHECK_RES(res);
      state.conflicting_name = 0; // searched for conflicting name once, no need to do it again
    }
  }

  return res;
}
#endif // !PSRAMFS_READ_ONLY

#if PSRAMFS_TEMPORAL_FD_CACHE
// djb2 hash
static u32_t psramfs_hash(psram *fs, const u8_t *name) {
  (void)fs;
  u32_t hash = 5381;
  u8_t c;
  int i = 0;
  while ((c = name[i++]) && i < PSRAMFS_OBJ_NAME_LEN) {
    hash = (hash * 33) ^ c;
  }
  return hash;
}
#endif

s32_t psramfs_fd_find_new(psram *fs, psramfs_fd **fd, const char *name) {
#if PSRAMFS_TEMPORAL_FD_CACHE
  u32_t i;
  u16_t min_score = 0xffff;
  u32_t cand_ix = (u32_t)-1;
  u32_t name_hash = name ? psramfs_hash(fs, (const u8_t *)name) : 0;
  psramfs_fd *fds = (psramfs_fd *)fs->fd_space;

  if (name) {
    // first, decrease score of all closed descriptors
    for (i = 0; i < fs->fd_count; i++) {
      psramfs_fd *cur_fd = &fds[i];
      if (cur_fd->file_nbr == 0) {
        if (cur_fd->score > 1) { // score == 0 indicates never used fd
          cur_fd->score--;
        }
      }
    }
  }

  // find the free fd with least score or name match
  for (i = 0; i < fs->fd_count; i++) {
    psramfs_fd *cur_fd = &fds[i];
    if (cur_fd->file_nbr == 0) {
      if (name && cur_fd->name_hash == name_hash) {
        cand_ix = i;
        break;
      }
      if (cur_fd->score < min_score) {
        min_score = cur_fd->score;
        cand_ix = i;
      }
    }
  }

  if (cand_ix != (u32_t)-1) {
    psramfs_fd *cur_fd = &fds[cand_ix];
    if (name) {
      if (cur_fd->name_hash == name_hash && cur_fd->score > 0) {
        // opened an fd with same name hash, assume same file
        // set search point to saved obj index page and hope we have a correct match directly
        // when start searching - if not, we will just keep searching until it is found
        fs->cursor_block_ix = PSRAMFS_BLOCK_FOR_PAGE(fs, cur_fd->objix_hdr_pix);
        fs->cursor_obj_lu_entry = PSRAMFS_OBJ_LOOKUP_ENTRY_FOR_PAGE(fs, cur_fd->objix_hdr_pix);
        // update score
        if (cur_fd->score < 0xffff-PSRAMFS_TEMPORAL_CACHE_HIT_SCORE) {
          cur_fd->score += PSRAMFS_TEMPORAL_CACHE_HIT_SCORE;
        } else {
          cur_fd->score = 0xffff;
        }
      } else {
        // no hash hit, restore this fd to initial state
        cur_fd->score = PSRAMFS_TEMPORAL_CACHE_HIT_SCORE;
        cur_fd->name_hash = name_hash;
      }
    }
    cur_fd->file_nbr = cand_ix+1;
    *fd = cur_fd;
    return PSRAMFS_OK;
  } else {
    return PSRAMFS_ERR_OUT_OF_FILE_DESCS;
  }
#else
  (void)name;
  u32_t i;
  psramfs_fd *fds = (psramfs_fd *)fs->fd_space;
  for (i = 0; i < fs->fd_count; i++) {
    psramfs_fd *cur_fd = &fds[i];
    if (cur_fd->file_nbr == 0) {
      cur_fd->file_nbr = i+1;
      *fd = cur_fd;
      return PSRAMFS_OK;
    }
  }
  return PSRAMFS_ERR_OUT_OF_FILE_DESCS;
#endif
}

s32_t psramfs_fd_return(psram *fs, psramfs_file f) {
  if (f <= 0 || f > (s16_t)fs->fd_count) {
    return PSRAMFS_ERR_BAD_DESCRIPTOR;
  }
  psramfs_fd *fds = (psramfs_fd *)fs->fd_space;
  psramfs_fd *fd = &fds[f-1];
  if (fd->file_nbr == 0) {
    return PSRAMFS_ERR_FILE_CLOSED;
  }
  fd->file_nbr = 0;
#if PSRAMFS_IX_MAP
  fd->ix_map = 0;
#endif
  return PSRAMFS_OK;
}

s32_t psramfs_fd_get(psram *fs, psramfs_file f, psramfs_fd **fd) {
  if (f <= 0 || f > (s16_t)fs->fd_count) {
    return PSRAMFS_ERR_BAD_DESCRIPTOR;
  }
  psramfs_fd *fds = (psramfs_fd *)fs->fd_space;
  *fd = &fds[f-1];
  if ((*fd)->file_nbr == 0) {
    return PSRAMFS_ERR_FILE_CLOSED;
  }
  return PSRAMFS_OK;
}

#if PSRAMFS_TEMPORAL_FD_CACHE
void psramfs_fd_temporal_cache_rehash(
    psram *fs,
    const char *old_path,
    const char *new_path) {
  u32_t i;
  u32_t old_hash = psramfs_hash(fs, (const u8_t *)old_path);
  u32_t new_hash = psramfs_hash(fs, (const u8_t *)new_path);
  psramfs_fd *fds = (psramfs_fd *)fs->fd_space;
  for (i = 0; i < fs->fd_count; i++) {
    psramfs_fd *cur_fd = &fds[i];
    if (cur_fd->score > 0 && cur_fd->name_hash == old_hash) {
      cur_fd->name_hash = new_hash;
    }
  }
}
#endif
