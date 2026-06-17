# Meatloaf Memory Optimization TODO

**Created:** 2026-06-16
**Updated:** 2026-06-16
**Status:** PARTIALLY COMPLETED (P0, P1, P3 done; P2 deferred)
**Priority Scale:** P0 (Safety/Crash) → P1 (Memory) → P2 (Cleanup) → P3 (Perf)

---

## EXECUTIVE SUMMARY

Completed optimizations address critical thread-safety, unbounded cache growth, and PSRAM allocation issues. All P0, P1, and P3 items are done. P2 (class consolidation) is marked deferred as it requires significant refactoring of network and disk media classes.

**Files Modified:** 6 files, +130/-85 lines
- `lib/meatloaf/meat_session.h`
- `lib/meatloaf/meat_session.cpp`
- `lib/meatloaf/meat_media.h`
- `lib/meatloaf/meat_media.cpp`
- `lib/meatloaf/meat_buffer.h`
- `lib/meatloaf/meatloaf.h`

---

## P0 - CRITICAL (Safety/Stability) - ALL COMPLETE

### TODO 1: Fix `CachedFile::s_rangeUsed` Thread Safety - DONE
**Files:** `meat_session.h`, `meat_session.cpp`
**Issue:** Race condition in HIMEM range allocation counter.
**Impact:** CRITICAL - prevents heap corruption from concurrent access.

**Changes:**
- [x] `static int s_rangeUsed` -> `static std::atomic<int> s_rangeUsed`
- [x] `s_rangeUsed++` -> `s_rangeUsed.fetch_add(1)`
- [x] `s_rangeUsed--` -> `s_rangeUsed.fetch_sub(1)` with proper range-free check

---

### TODO 2: Add ImageBroker LRU Eviction - DONE
**Files:** `meat_media.h`, `meat_media.cpp`
**Issue:** Unbounded `image_repo` growth.
**Impact:** HIGH - prevents memory exhaustion from many disk images.

**Changes:**
- [x] Added `LRUEntry` struct with key + timestamp
- [x] Added `lru_order` vector (oldest-first LRU list)
- [x] `max_entries = 50`, `cleanup_interval_ms = 60000`
- [x] `is_in_use()` - checks if entry is active (matches any drive CWD)
- [x] `evict_lru_if_needed()` - evicts oldest NOT-in-use entry when at capacity
- [x] `cleanup_old_entries()` - removes stale NOT-in-use entries every 60s
- [x] `touch_entry()` - updates LRU position on cache hit
- [x] **Important:** Eviction skips entries that are currently mounted/in use

---

## P1 - HIGH (Memory Usage) - ALL COMPLETE

### TODO 3: Move `mfilebuf` Buffers to PSRAM - DONE
**Files:** `meat_buffer.h`
**Issue:** 2.5KB heap allocation per open file.
**Impact:** MEDIUM - saves ~2.5KB internal DRAM per concurrent file.

**Changes:**
- [x] `new char[]` -> `heap_caps_malloc()` with PSRAM + heap fallback
- [x] `delete[]` -> `free()` (required for heap_caps_malloc)

---

### TODO 4: Add MSession file_cache Size Limits - DONE
**Files:** `meat_session.h`
**Issue:** Unbounded per-session file cache.
**Impact:** MEDIUM - prevents RAM exhaustion from many cached files.

**Changes:**
- [x] Added `max_file_cache_entries = 10`
- [x] Added `cache_order` list for LRU tracking
- [x] `cacheFile()` evicts oldest when at capacity
- [x] `getCachedFile()` moves accessed entry to front

---

### TODO 5: SessionBroker Shutdown Fast Path + Idle Session Cleanup - DONE
**Files:** `meat_session.h`
**Issue:** Shutdown not checked at loop start; sessions not checked for active drive/console usage.
**Impact:** MEDIUM - prevents stale sessions from accumulating.

**Changes:**
- [x] Added `if (system_shutdown) break;` at loop start
- [x] Added `session_host_key()` - extracts scheme://host (stripping port) for comparison
- [x] Added `path_matches_session()` - compares paths ignoring port numbers
- [x] Added `is_session_in_use(key)` - checks drives AND console for session usage
- [x] `service()` now removes NOT-in-use sessions immediately (no keep-alive)
- [x] `service()` attempts reconnect for in-use sessions if keep-alive fails

---

## P2 - MEDIUM (Class Consolidation) - DEFERRED

### TODO 6: Create NetworkMFile Base Class - DEFERRED
**Reason:** Requires significant refactoring of 5+ network filesystem classes. Low priority since memory issues are addressed.

### TODO 7: Create NetworkMStream Base Class - DEFERRED
**Reason:** Requires TODO 6 first.

### TODO 8: Consolidate Disk Media Stream Base Class - DEFERRED
**Reason:** D64/D71/D81/D82 have diverged significantly. Consolidation benefit is low.

---

## P3 - LOW (Performance) - ALL COMPLETE

### TODO 9: Static Buffer in `CachedFile::loadFromStream()` - DONE
**Files:** `meat_session.cpp`
**Issue:** Per-call heap allocation in stream loading.
**Impact:** LOW - reduces allocation churn.

**Changes:**
- [x] `psram_malloc()` per call -> `thread_local` static buffer (reused)
- [x] Removed `free()` calls (buffer persists)

---

### TODO 10: Archive Buffer PSRAM Allocation - ALREADY DONE
**Note:** `m_srcBuffer` already uses `psram_malloc()` with heap fallback.

---

## REMAINING ARCHITECTURAL ISSUES (Not Addressed)

| Issue | Impact | Notes |
|-------|--------|-------|
| String fragmentation (PeoplesUrlParser 13 strings) | ~37 bytes/file listing leak | Architectural - requires 91+ call site changes |
| MFile chain depth (5+ levels) | Memory pressure from nested containers | Acceptable for current use patterns |
| Network stream buffer reuse | Minor allocation overhead | Low priority |

---

## TESTING CHECKLIST

- [ ] Build succeeds with `-Wall -Wextra`
- [ ] No exceptions during `Debug_memory()` output
- [ ] IEC bus still functions (drive mounting, file loading)
- [ ] Directory listing doesn't leak over 100 iterations
- [ ] `seekFileSize()` on large D64 doesn't crash
- [ ] Concurrent HTTP + TNFS access doesn't corrupt heap
- [ ] ImageBroker LRU eviction triggers at 50+ entries
- [ ] SessionBroker shutdown exits cleanly
- [ ] mfilebuf PSRAM allocation works (check via `Debug_memory()`)
