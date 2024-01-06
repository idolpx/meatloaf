#include "psram.h"
#include "psramfs_nucleus.h"

#if !PSRAMFS_READ_ONLY

// Erases a logical block and updates the erase counter.
// If cache is enabled, all pages that might be cached in this block
// is dropped.
static s32_t psramfs_gc_erase_block(
    psram *fs,
    psramfs_block_ix bix) {
  s32_t res;

  PSRAMFS_GC_DBG("gc: erase block "_SPIPRIbl"\n", bix);
  res = psramfs_erase_block(fs, bix);
  PSRAMFS_CHECK_RES(res);

#if PSRAMFS_CACHE
  {
    u32_t i;
    for (i = 0; i < PSRAMFS_PAGES_PER_BLOCK(fs); i++) {
      psramfs_cache_drop_page(fs, PSRAMFS_PAGE_FOR_BLOCK(fs, bix) + i);
    }
  }
#endif
  return res;
}

// Searches for blocks where all entries are deleted - if one is found,
// the block is erased. Compared to the non-quick gc, the quick one ensures
// that no updates are needed on existing objects on pages that are erased.
s32_t psramfs_gc_quick(
    psram *fs, u16_t max_free_pages) {
  s32_t res = PSRAMFS_OK;
  u32_t blocks = fs->block_count;
  psramfs_block_ix cur_block = 0;
  u32_t cur_block_addr = 0;
  int cur_entry = 0;
  psramfs_obj_id *obj_lu_buf = (psramfs_obj_id *)fs->lu_work;

  PSRAMFS_GC_DBG("gc_quick: running\n");
#if PSRAMFS_GC_STATS
  fs->stats_gc_runs++;
#endif

  int entries_per_page = (PSRAMFS_CFG_LOG_PAGE_SZ(fs) / sizeof(psramfs_obj_id));

  // find fully deleted blocks
  // check each block
  while (res == PSRAMFS_OK && blocks--) {
    u16_t deleted_pages_in_block = 0;
    u16_t free_pages_in_block = 0;

    int obj_lookup_page = 0;
    // check each object lookup page
    while (res == PSRAMFS_OK && obj_lookup_page < (int)PSRAMFS_OBJ_LOOKUP_PAGES(fs)) {
      int entry_offset = obj_lookup_page * entries_per_page;
      res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_READ,
          0, cur_block_addr + PSRAMFS_PAGE_TO_PADDR(fs, obj_lookup_page), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->lu_work);
      // check each entry
      while (res == PSRAMFS_OK &&
          cur_entry - entry_offset < entries_per_page &&
          cur_entry < (int)(PSRAMFS_PAGES_PER_BLOCK(fs)-PSRAMFS_OBJ_LOOKUP_PAGES(fs))) {
        psramfs_obj_id obj_id = obj_lu_buf[cur_entry-entry_offset];
        if (obj_id == PSRAMFS_OBJ_ID_DELETED) {
          deleted_pages_in_block++;
        } else if (obj_id == PSRAMFS_OBJ_ID_FREE) {
          // kill scan, go for next block
          free_pages_in_block++;
          if (free_pages_in_block > max_free_pages) {
            obj_lookup_page = PSRAMFS_OBJ_LOOKUP_PAGES(fs);
            res = 1; // kill object lu loop
            break;
          }
        }  else {
          // kill scan, go for next block
          obj_lookup_page = PSRAMFS_OBJ_LOOKUP_PAGES(fs);
          res = 1; // kill object lu loop
          break;
        }
        cur_entry++;
      } // per entry
      obj_lookup_page++;
    } // per object lookup page
    if (res == 1) res = PSRAMFS_OK;

    if (res == PSRAMFS_OK &&
        deleted_pages_in_block + free_pages_in_block == PSRAMFS_PAGES_PER_BLOCK(fs)-PSRAMFS_OBJ_LOOKUP_PAGES(fs) &&
        free_pages_in_block <= max_free_pages) {
      // found a fully deleted block
      fs->stats_p_deleted -= deleted_pages_in_block;
      res = psramfs_gc_erase_block(fs, cur_block);
      return res;
    }

    cur_entry = 0;
    cur_block++;
    cur_block_addr += PSRAMFS_CFG_LOG_BLOCK_SZ(fs);
  } // per block

  if (res == PSRAMFS_OK) {
    res = PSRAMFS_ERR_NO_DELETED_BLOCKS;
  }
  return res;
}

// Checks if garbage collecting is necessary. If so a candidate block is found,
// cleansed and erased
s32_t psramfs_gc_check(
    psram *fs,
    u32_t len) {
  s32_t res;
  s32_t free_pages =
      (PSRAMFS_PAGES_PER_BLOCK(fs) - PSRAMFS_OBJ_LOOKUP_PAGES(fs)) * (fs->block_count-2)
      - fs->stats_p_allocated - fs->stats_p_deleted;
  int tries = 0;

  if (fs->free_blocks > 3 &&
      (s32_t)len < free_pages * (s32_t)PSRAMFS_DATA_PAGE_SIZE(fs)) {
    return PSRAMFS_OK;
  }

  u32_t needed_pages = (len + PSRAMFS_DATA_PAGE_SIZE(fs) - 1) / PSRAMFS_DATA_PAGE_SIZE(fs);
//  if (fs->free_blocks <= 2 && (s32_t)needed_pages > free_pages) {
//    PSRAMFS_GC_DBG("gc: full freeblk:"_SPIPRIi" needed:"_SPIPRIi" free:"_SPIPRIi" dele:"_SPIPRIi"\n", fs->free_blocks, needed_pages, free_pages, fs->stats_p_deleted);
//    return PSRAMFS_ERR_FULL;
//  }
  if ((s32_t)needed_pages > (s32_t)(free_pages + fs->stats_p_deleted)) {
    PSRAMFS_GC_DBG("gc_check: full freeblk:"_SPIPRIi" needed:"_SPIPRIi" free:"_SPIPRIi" dele:"_SPIPRIi"\n", fs->free_blocks, needed_pages, free_pages, fs->stats_p_deleted);
    return PSRAMFS_ERR_FULL;
  }

  do {
    PSRAMFS_GC_DBG("\ngc_check #"_SPIPRIi": run gc free_blocks:"_SPIPRIi" pfree:"_SPIPRIi" pallo:"_SPIPRIi" pdele:"_SPIPRIi" ["_SPIPRIi"] len:"_SPIPRIi" of "_SPIPRIi"\n",
        tries,
        fs->free_blocks, free_pages, fs->stats_p_allocated, fs->stats_p_deleted, (free_pages+fs->stats_p_allocated+fs->stats_p_deleted),
        len, (u32_t)(free_pages*PSRAMFS_DATA_PAGE_SIZE(fs)));

    psramfs_block_ix *cands;
    int count;
    psramfs_block_ix cand;
    s32_t prev_free_pages = free_pages;
    // if the fs is crammed, ignore block age when selecting candidate - kind of a bad state
    res = psramfs_gc_find_candidate(fs, &cands, &count, free_pages <= 0);
    PSRAMFS_CHECK_RES(res);
    if (count == 0) {
      PSRAMFS_GC_DBG("gc_check: no candidates, return\n");
      return (s32_t)needed_pages < free_pages ? PSRAMFS_OK : PSRAMFS_ERR_FULL;
    }
#if PSRAMFS_GC_STATS
    fs->stats_gc_runs++;
#endif
    cand = cands[0];
    fs->cleaning = 1;
    //PSRAMFS_GC_DBG("gcing: cleaning block "_SPIPRIi"\n", cand);
    res = psramfs_gc_clean(fs, cand);
    fs->cleaning = 0;
    if (res < 0) {
      PSRAMFS_GC_DBG("gc_check: cleaning block "_SPIPRIi", result "_SPIPRIi"\n", cand, res);
    } else {
      PSRAMFS_GC_DBG("gc_check: cleaning block "_SPIPRIi", result "_SPIPRIi"\n", cand, res);
    }
    PSRAMFS_CHECK_RES(res);

    res = psramfs_gc_erase_page_stats(fs, cand);
    PSRAMFS_CHECK_RES(res);

    res = psramfs_gc_erase_block(fs, cand);
    PSRAMFS_CHECK_RES(res);

    free_pages =
          (PSRAMFS_PAGES_PER_BLOCK(fs) - PSRAMFS_OBJ_LOOKUP_PAGES(fs)) * (fs->block_count - 2)
          - fs->stats_p_allocated - fs->stats_p_deleted;

    if (prev_free_pages <= 0 && prev_free_pages == free_pages) {
      // abort early to reduce wear, at least tried once
      PSRAMFS_GC_DBG("gc_check: early abort, no result on gc when fs crammed\n");
      break;
    }

  } while (++tries < PSRAMFS_GC_MAX_RUNS && (fs->free_blocks <= 2 ||
      (s32_t)len > free_pages*(s32_t)PSRAMFS_DATA_PAGE_SIZE(fs)));

  free_pages =
        (PSRAMFS_PAGES_PER_BLOCK(fs) - PSRAMFS_OBJ_LOOKUP_PAGES(fs)) * (fs->block_count - 2)
        - fs->stats_p_allocated - fs->stats_p_deleted;
  if ((s32_t)len > free_pages*(s32_t)PSRAMFS_DATA_PAGE_SIZE(fs)) {
    res = PSRAMFS_ERR_FULL;
  }

  PSRAMFS_GC_DBG("gc_check: finished, "_SPIPRIi" dirty, blocks "_SPIPRIi" free, "_SPIPRIi" pages free, "_SPIPRIi" tries, res "_SPIPRIi"\n",
      fs->stats_p_allocated + fs->stats_p_deleted,
      fs->free_blocks, free_pages, tries, res);

  return res;
}

// Updates page statistics for a block that is about to be erased
s32_t psramfs_gc_erase_page_stats(
    psram *fs,
    psramfs_block_ix bix) {
  s32_t res = PSRAMFS_OK;
  int obj_lookup_page = 0;
  int entries_per_page = (PSRAMFS_CFG_LOG_PAGE_SZ(fs) / sizeof(psramfs_obj_id));
  psramfs_obj_id *obj_lu_buf = (psramfs_obj_id *)fs->lu_work;
  int cur_entry = 0;
  u32_t dele = 0;
  u32_t allo = 0;

  // check each object lookup page
  while (res == PSRAMFS_OK && obj_lookup_page < (int)PSRAMFS_OBJ_LOOKUP_PAGES(fs)) {
    int entry_offset = obj_lookup_page * entries_per_page;
    res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_READ,
        0, bix * PSRAMFS_CFG_LOG_BLOCK_SZ(fs) + PSRAMFS_PAGE_TO_PADDR(fs, obj_lookup_page), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->lu_work);
    // check each entry
    while (res == PSRAMFS_OK &&
        cur_entry - entry_offset < entries_per_page && cur_entry < (int)(PSRAMFS_PAGES_PER_BLOCK(fs)-PSRAMFS_OBJ_LOOKUP_PAGES(fs))) {
      psramfs_obj_id obj_id = obj_lu_buf[cur_entry-entry_offset];
      if (obj_id == PSRAMFS_OBJ_ID_FREE) {
      } else if (obj_id == PSRAMFS_OBJ_ID_DELETED) {
        dele++;
      } else {
        allo++;
      }
      cur_entry++;
    } // per entry
    obj_lookup_page++;
  } // per object lookup page
  PSRAMFS_GC_DBG("gc_check: wipe pallo:"_SPIPRIi" pdele:"_SPIPRIi"\n", allo, dele);
  fs->stats_p_allocated -= allo;
  fs->stats_p_deleted -= dele;
  return res;
}

// Finds block candidates to erase
s32_t psramfs_gc_find_candidate(
    psram *fs,
    psramfs_block_ix **block_candidates,
    int *candidate_count,
    char fs_crammed) {
  s32_t res = PSRAMFS_OK;
  u32_t blocks = fs->block_count;
  psramfs_block_ix cur_block = 0;
  u32_t cur_block_addr = 0;
  psramfs_obj_id *obj_lu_buf = (psramfs_obj_id *)fs->lu_work;
  int cur_entry = 0;

  // using fs->work area as sorted candidate memory, (psramfs_block_ix)cand_bix/(s32_t)score
  int max_candidates = MIN(fs->block_count, (PSRAMFS_CFG_LOG_PAGE_SZ(fs)-8)/(sizeof(psramfs_block_ix) + sizeof(s32_t)));
  *candidate_count = 0;
  memset(fs->work, 0xff, PSRAMFS_CFG_LOG_PAGE_SZ(fs));

  // divide up work area into block indices and scores
  psramfs_block_ix *cand_blocks = (psramfs_block_ix *)fs->work;
  s32_t *cand_scores = (s32_t *)(fs->work + max_candidates * sizeof(psramfs_block_ix));

   // align cand_scores on s32_t boundary
  cand_scores = (s32_t*)(((intptr_t)cand_scores + sizeof(intptr_t) - 1) & ~(sizeof(intptr_t) - 1));

  *block_candidates = cand_blocks;

  int entries_per_page = (PSRAMFS_CFG_LOG_PAGE_SZ(fs) / sizeof(psramfs_obj_id));

  // check each block
  while (res == PSRAMFS_OK && blocks--) {
    u16_t deleted_pages_in_block = 0;
    u16_t used_pages_in_block = 0;

    int obj_lookup_page = 0;
    // check each object lookup page
    while (res == PSRAMFS_OK && obj_lookup_page < (int)PSRAMFS_OBJ_LOOKUP_PAGES(fs)) {
      int entry_offset = obj_lookup_page * entries_per_page;
      res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_READ,
          0, cur_block_addr + PSRAMFS_PAGE_TO_PADDR(fs, obj_lookup_page), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->lu_work);
      // check each entry
      while (res == PSRAMFS_OK &&
          cur_entry - entry_offset < entries_per_page &&
          cur_entry < (int)(PSRAMFS_PAGES_PER_BLOCK(fs)-PSRAMFS_OBJ_LOOKUP_PAGES(fs))) {
        psramfs_obj_id obj_id = obj_lu_buf[cur_entry-entry_offset];
        if (obj_id == PSRAMFS_OBJ_ID_FREE) {
          // when a free entry is encountered, scan logic ensures that all following entries are free also
          res = 1; // kill object lu loop
          break;
        } else  if (obj_id == PSRAMFS_OBJ_ID_DELETED) {
          deleted_pages_in_block++;
        } else {
          used_pages_in_block++;
        }
        cur_entry++;
      } // per entry
      obj_lookup_page++;
    } // per object lookup page
    if (res == 1) res = PSRAMFS_OK;

    // calculate score and insert into candidate table
    // stoneage sort, but probably not so many blocks
    if (res == PSRAMFS_OK /*&& deleted_pages_in_block > 0*/) {
      // read erase count
      psramfs_obj_id erase_count;
      res = _psramfs_rd(fs, PSRAMFS_OP_C_READ | PSRAMFS_OP_T_OBJ_LU2, 0,
          PSRAMFS_ERASE_COUNT_PADDR(fs, cur_block),
          sizeof(psramfs_obj_id), (u8_t *)&erase_count);
      PSRAMFS_CHECK_RES(res);

      psramfs_obj_id erase_age;
      if (fs->max_erase_count > erase_count) {
        erase_age = fs->max_erase_count - erase_count;
      } else {
        erase_age = PSRAMFS_OBJ_ID_FREE - (erase_count - fs->max_erase_count);
      }

      s32_t score =
          deleted_pages_in_block * PSRAMFS_GC_HEUR_W_DELET +
          used_pages_in_block * PSRAMFS_GC_HEUR_W_USED +
          erase_age * (fs_crammed ? 0 : PSRAMFS_GC_HEUR_W_ERASE_AGE);
      int cand_ix = 0;
      PSRAMFS_GC_DBG("gc_check: bix:"_SPIPRIbl" del:"_SPIPRIi" use:"_SPIPRIi" score:"_SPIPRIi"\n", cur_block, deleted_pages_in_block, used_pages_in_block, score);
      while (cand_ix < max_candidates) {
        if (cand_blocks[cand_ix] == (psramfs_block_ix)-1) {
          cand_blocks[cand_ix] = cur_block;
          cand_scores[cand_ix] = score;
          break;
        } else if (cand_scores[cand_ix] < score) {
          int reorder_cand_ix = max_candidates - 2;
          while (reorder_cand_ix >= cand_ix) {
            cand_blocks[reorder_cand_ix + 1] = cand_blocks[reorder_cand_ix];
            cand_scores[reorder_cand_ix + 1] = cand_scores[reorder_cand_ix];
            reorder_cand_ix--;
          }
          cand_blocks[cand_ix] = cur_block;
          cand_scores[cand_ix] = score;
          break;
        }
        cand_ix++;
      }
      (*candidate_count)++;
    }

    cur_entry = 0;
    cur_block++;
    cur_block_addr += PSRAMFS_CFG_LOG_BLOCK_SZ(fs);
  } // per block

  return res;
}

typedef enum {
  FIND_OBJ_DATA,
  MOVE_OBJ_DATA,
  MOVE_OBJ_IX,
  FINISHED
} psramfs_gc_clean_state;

typedef struct {
  psramfs_gc_clean_state state;
  psramfs_obj_id cur_obj_id;
  psramfs_span_ix cur_objix_spix;
  psramfs_page_ix cur_objix_pix;
  psramfs_page_ix cur_data_pix;
  int stored_scan_entry_index;
  u8_t obj_id_found;
} psramfs_gc;

// Empties given block by moving all data into free pages of another block
// Strategy:
//   loop:
//   scan object lookup for object data pages
//   for first found id, check spix and load corresponding object index page to memory
//   push object scan lookup entry index
//     rescan object lookup, find data pages with same id and referenced by same object index
//     move data page, update object index in memory
//     when reached end of lookup, store updated object index
//   pop object scan lookup entry index
//   repeat loop until end of object lookup
//   scan object lookup again for remaining object index pages, move to new page in other block
//
s32_t psramfs_gc_clean(psram *fs, psramfs_block_ix bix) {
  s32_t res = PSRAMFS_OK;
  const int entries_per_page = (PSRAMFS_CFG_LOG_PAGE_SZ(fs) / sizeof(psramfs_obj_id));
  // this is the global localizer being pushed and popped
  int cur_entry = 0;
  psramfs_obj_id *obj_lu_buf = (psramfs_obj_id *)fs->lu_work;
  psramfs_gc gc; // our stack frame/state
  psramfs_page_ix cur_pix = 0;
  psramfs_page_object_ix_header *objix_hdr = (psramfs_page_object_ix_header *)fs->work;
  psramfs_page_object_ix *objix = (psramfs_page_object_ix *)fs->work;

  PSRAMFS_GC_DBG("gc_clean: cleaning block "_SPIPRIbl"\n", bix);

  memset(&gc, 0, sizeof(psramfs_gc));
  gc.state = FIND_OBJ_DATA;

  if (fs->free_cursor_block_ix == bix) {
    // move free cursor to next block, cannot use free pages from the block we want to clean
    fs->free_cursor_block_ix = (bix+1)%fs->block_count;
    fs->free_cursor_obj_lu_entry = 0;
    PSRAMFS_GC_DBG("gc_clean: move free cursor to block "_SPIPRIbl"\n", fs->free_cursor_block_ix);
  }

  while (res == PSRAMFS_OK && gc.state != FINISHED) {
    PSRAMFS_GC_DBG("gc_clean: state = "_SPIPRIi" entry:"_SPIPRIi"\n", gc.state, cur_entry);
    gc.obj_id_found = 0; // reset (to no found data page)

    // scan through lookup pages
    int obj_lookup_page = cur_entry / entries_per_page;
    u8_t scan = 1;
    // check each object lookup page
    while (scan && res == PSRAMFS_OK && obj_lookup_page < (int)PSRAMFS_OBJ_LOOKUP_PAGES(fs)) {
      int entry_offset = obj_lookup_page * entries_per_page;
      res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_READ,
          0, bix * PSRAMFS_CFG_LOG_BLOCK_SZ(fs) + PSRAMFS_PAGE_TO_PADDR(fs, obj_lookup_page),
          PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->lu_work);
      // check each object lookup entry
      while (scan && res == PSRAMFS_OK &&
          cur_entry - entry_offset < entries_per_page && cur_entry < (int)(PSRAMFS_PAGES_PER_BLOCK(fs)-PSRAMFS_OBJ_LOOKUP_PAGES(fs))) {
        psramfs_obj_id obj_id = obj_lu_buf[cur_entry-entry_offset];
        cur_pix = PSRAMFS_OBJ_LOOKUP_ENTRY_TO_PIX(fs, bix, cur_entry);

        // act upon object id depending on gc state
        switch (gc.state) {
        case FIND_OBJ_DATA:
          // find a data page
          if (obj_id != PSRAMFS_OBJ_ID_DELETED && obj_id != PSRAMFS_OBJ_ID_FREE &&
              ((obj_id & PSRAMFS_OBJ_ID_IX_FLAG) == 0)) {
            // found a data page, stop scanning and handle in switch case below
            PSRAMFS_GC_DBG("gc_clean: FIND_DATA state:"_SPIPRIi" - found obj id "_SPIPRIid"\n", gc.state, obj_id);
            gc.obj_id_found = 1;
            gc.cur_obj_id = obj_id;
            gc.cur_data_pix = cur_pix;
            scan = 0;
          }
          break;
        case MOVE_OBJ_DATA:
          // evacuate found data pages for corresponding object index we have in memory,
          // update memory representation
          if (obj_id == gc.cur_obj_id) {
            psramfs_page_header p_hdr;
            res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
                0, PSRAMFS_PAGE_TO_PADDR(fs, cur_pix), sizeof(psramfs_page_header), (u8_t*)&p_hdr);
            PSRAMFS_CHECK_RES(res);
            PSRAMFS_GC_DBG("gc_clean: MOVE_DATA found data page "_SPIPRIid":"_SPIPRIsp" @ "_SPIPRIpg"\n", gc.cur_obj_id, p_hdr.span_ix, cur_pix);
            if (PSRAMFS_OBJ_IX_ENTRY_SPAN_IX(fs, p_hdr.span_ix) != gc.cur_objix_spix) {
              PSRAMFS_GC_DBG("gc_clean: MOVE_DATA no objix spix match, take in another run\n");
            } else {
              psramfs_page_ix new_data_pix;
              if (p_hdr.flags & PSRAMFS_PH_FLAG_DELET) {
                // move page
                res = psramfs_page_move(fs, 0, 0, obj_id, &p_hdr, cur_pix, &new_data_pix);
                PSRAMFS_GC_DBG("gc_clean: MOVE_DATA move objix "_SPIPRIid":"_SPIPRIsp" page "_SPIPRIpg" to "_SPIPRIpg"\n", gc.cur_obj_id, p_hdr.span_ix, cur_pix, new_data_pix);
                PSRAMFS_CHECK_RES(res);
                // move wipes obj_lu, reload it
                res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_READ,
                    0, bix * PSRAMFS_CFG_LOG_BLOCK_SZ(fs) + PSRAMFS_PAGE_TO_PADDR(fs, obj_lookup_page),
                    PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->lu_work);
                PSRAMFS_CHECK_RES(res);
              } else {
                // page is deleted but not deleted in lookup, scrap it -
                // might seem unnecessary as we will erase this block, but
                // we might get aborted
                PSRAMFS_GC_DBG("gc_clean: MOVE_DATA wipe objix "_SPIPRIid":"_SPIPRIsp" page "_SPIPRIpg"\n", obj_id, p_hdr.span_ix, cur_pix);
                res = psramfs_page_delete(fs, cur_pix);
                PSRAMFS_CHECK_RES(res);
                new_data_pix = PSRAMFS_OBJ_ID_FREE;
              }
              // update memory representation of object index page with new data page
              if (gc.cur_objix_spix == 0) {
                // update object index header page
                ((psramfs_page_ix*)((u8_t *)objix_hdr + sizeof(psramfs_page_object_ix_header)))[p_hdr.span_ix] = new_data_pix;
                PSRAMFS_GC_DBG("gc_clean: MOVE_DATA wrote page "_SPIPRIpg" to objix_hdr entry "_SPIPRIsp" in mem\n", new_data_pix, (psramfs_span_ix)PSRAMFS_OBJ_IX_ENTRY(fs, p_hdr.span_ix));
              } else {
                // update object index page
                ((psramfs_page_ix*)((u8_t *)objix + sizeof(psramfs_page_object_ix)))[PSRAMFS_OBJ_IX_ENTRY(fs, p_hdr.span_ix)] = new_data_pix;
                PSRAMFS_GC_DBG("gc_clean: MOVE_DATA wrote page "_SPIPRIpg" to objix entry "_SPIPRIsp" in mem\n", new_data_pix, (psramfs_span_ix)PSRAMFS_OBJ_IX_ENTRY(fs, p_hdr.span_ix));
              }
            }
          }
          break;
        case MOVE_OBJ_IX:
          // find and evacuate object index pages
          if (obj_id != PSRAMFS_OBJ_ID_DELETED && obj_id != PSRAMFS_OBJ_ID_FREE &&
              (obj_id & PSRAMFS_OBJ_ID_IX_FLAG)) {
            // found an index object id
            psramfs_page_header p_hdr;
            psramfs_page_ix new_pix;
            // load header
            res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
                0, PSRAMFS_PAGE_TO_PADDR(fs, cur_pix), sizeof(psramfs_page_header), (u8_t*)&p_hdr);
            PSRAMFS_CHECK_RES(res);
            if (p_hdr.flags & PSRAMFS_PH_FLAG_DELET) {
              // move page
              res = psramfs_page_move(fs, 0, 0, obj_id, &p_hdr, cur_pix, &new_pix);
              PSRAMFS_GC_DBG("gc_clean: MOVE_OBJIX move objix "_SPIPRIid":"_SPIPRIsp" page "_SPIPRIpg" to "_SPIPRIpg"\n", obj_id, p_hdr.span_ix, cur_pix, new_pix);
              PSRAMFS_CHECK_RES(res);
              psramfs_cb_object_event(fs, (psramfs_page_object_ix *)&p_hdr,
                  PSRAMFS_EV_IX_MOV, obj_id, p_hdr.span_ix, new_pix, 0);
              // move wipes obj_lu, reload it
              res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU | PSRAMFS_OP_C_READ,
                  0, bix * PSRAMFS_CFG_LOG_BLOCK_SZ(fs) + PSRAMFS_PAGE_TO_PADDR(fs, obj_lookup_page),
                  PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->lu_work);
              PSRAMFS_CHECK_RES(res);
            } else {
              // page is deleted but not deleted in lookup, scrap it -
              // might seem unnecessary as we will erase this block, but
              // we might get aborted
              PSRAMFS_GC_DBG("gc_clean: MOVE_OBJIX wipe objix "_SPIPRIid":"_SPIPRIsp" page "_SPIPRIpg"\n", obj_id, p_hdr.span_ix, cur_pix);
              res = psramfs_page_delete(fs, cur_pix);
              if (res == PSRAMFS_OK) {
                psramfs_cb_object_event(fs, (psramfs_page_object_ix *)0,
                    PSRAMFS_EV_IX_DEL, obj_id, p_hdr.span_ix, cur_pix, 0);
              }
            }
            PSRAMFS_CHECK_RES(res);
          }
          break;
        default:
          scan = 0;
          break;
        } // switch gc state
        cur_entry++;
      } // per entry
      obj_lookup_page++; // no need to check scan variable here, obj_lookup_page is set in start of loop
    } // per object lookup page
    if (res != PSRAMFS_OK) break;

    // state finalization and switch
    switch (gc.state) {
    case FIND_OBJ_DATA:
      if (gc.obj_id_found) {
        // handle found data page -
        // find out corresponding obj ix page and load it to memory
        psramfs_page_header p_hdr;
        psramfs_page_ix objix_pix;
        gc.stored_scan_entry_index = cur_entry; // push cursor
        cur_entry = 0; // restart scan from start
        gc.state = MOVE_OBJ_DATA;
        res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
            0, PSRAMFS_PAGE_TO_PADDR(fs, cur_pix), sizeof(psramfs_page_header), (u8_t*)&p_hdr);
        PSRAMFS_CHECK_RES(res);
        gc.cur_objix_spix = PSRAMFS_OBJ_IX_ENTRY_SPAN_IX(fs, p_hdr.span_ix);
        PSRAMFS_GC_DBG("gc_clean: FIND_DATA find objix span_ix:"_SPIPRIsp"\n", gc.cur_objix_spix);
        res = psramfs_obj_lu_find_id_and_span(fs, gc.cur_obj_id | PSRAMFS_OBJ_ID_IX_FLAG, gc.cur_objix_spix, 0, &objix_pix);
        if (res == PSRAMFS_ERR_NOT_FOUND) {
          // on borked systems we might get an ERR_NOT_FOUND here -
          // this is handled by simply deleting the page as it is not referenced
          // from anywhere
          PSRAMFS_GC_DBG("gc_clean: FIND_OBJ_DATA objix not found! Wipe page "_SPIPRIpg"\n", gc.cur_data_pix);
          res = psramfs_page_delete(fs, gc.cur_data_pix);
          PSRAMFS_CHECK_RES(res);
          // then we restore states and continue scanning for data pages
          cur_entry = gc.stored_scan_entry_index; // pop cursor
          gc.state = FIND_OBJ_DATA;
          break; // done
        }
        PSRAMFS_CHECK_RES(res);
        PSRAMFS_GC_DBG("gc_clean: FIND_DATA found object index at page "_SPIPRIpg"\n", objix_pix);
        res = _psramfs_rd(fs, PSRAMFS_OP_T_OBJ_LU2 | PSRAMFS_OP_C_READ,
            0, PSRAMFS_PAGE_TO_PADDR(fs, objix_pix), PSRAMFS_CFG_LOG_PAGE_SZ(fs), fs->work);
        PSRAMFS_CHECK_RES(res);
        // cannot allow a gc if the presumed index in fact is no index, a
        // check must run or lot of data may be lost
        PSRAMFS_VALIDATE_OBJIX(objix->p_hdr, gc.cur_obj_id | PSRAMFS_OBJ_ID_IX_FLAG, gc.cur_objix_spix);
        gc.cur_objix_pix = objix_pix;
      } else {
        // no more data pages found, passed thru all block, start evacuating object indices
        gc.state = MOVE_OBJ_IX;
        cur_entry = 0; // restart entry scan index
      }
      break;
    case MOVE_OBJ_DATA: {
      // store modified objix (hdr) page residing in memory now that all
      // data pages belonging to this object index and residing in the block
      // we want to evacuate
      psramfs_page_ix new_objix_pix;
      gc.state = FIND_OBJ_DATA;
      cur_entry = gc.stored_scan_entry_index; // pop cursor
      if (gc.cur_objix_spix == 0) {
        // store object index header page
        res = psramfs_object_update_index_hdr(fs, 0, gc.cur_obj_id | PSRAMFS_OBJ_ID_IX_FLAG, gc.cur_objix_pix, fs->work, 0, 0, 0, &new_objix_pix);
        PSRAMFS_GC_DBG("gc_clean: MOVE_DATA store modified objix_hdr page, "_SPIPRIpg":"_SPIPRIsp"\n", new_objix_pix, 0);
        PSRAMFS_CHECK_RES(res);
      } else {
        // store object index page
        res = psramfs_page_move(fs, 0, fs->work, gc.cur_obj_id | PSRAMFS_OBJ_ID_IX_FLAG, 0, gc.cur_objix_pix, &new_objix_pix);
        PSRAMFS_GC_DBG("gc_clean: MOVE_DATA store modified objix page, "_SPIPRIpg":"_SPIPRIsp"\n", new_objix_pix, objix->p_hdr.span_ix);
        PSRAMFS_CHECK_RES(res);
        psramfs_cb_object_event(fs, (psramfs_page_object_ix *)fs->work,
            PSRAMFS_EV_IX_UPD, gc.cur_obj_id, objix->p_hdr.span_ix, new_objix_pix, 0);
      }
    }
    break;
    case MOVE_OBJ_IX:
      // scanned thru all block, no more object indices found - our work here is done
      gc.state = FINISHED;
      break;
    default:
      cur_entry = 0;
      break;
    } // switch gc.state
    PSRAMFS_GC_DBG("gc_clean: state-> "_SPIPRIi"\n", gc.state);
  } // while state != FINISHED


  return res;
}

#endif // !PSRAMFS_READ_ONLY
