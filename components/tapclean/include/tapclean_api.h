/*
 * tapclean_api.h - embedded library API for the vendored TAPClean engine
 *
 * TAPClean is (C) 2001-2006 Stewart Wilson, Subchrist Software and the
 * TAPClean contributors, GPL v2 or later (see src/ headers). This wrapper
 * adapts the engine for use as an in-memory analysis library: the whole
 * TAP image is scanned once and the resulting block/PRG databases are
 * exposed read-only. Not thread-safe - the engine uses global state, so
 * serialize all calls (Meatloaf only decodes one tape at a time).
 */

#ifndef TAPCLEAN_API_H
#define TAPCLEAN_API_H

#ifdef __cplusplus
extern "C" {
#endif

#define TAPCLEAN_MACHINE_C64   0
#define TAPCLEAN_MACHINE_VIC20 1
#define TAPCLEAN_MACHINE_C16   2

/* One recognized entity from the scan (data block, pause or gap) */
typedef struct {
    int type;                  /* loader type id (internal enum value) */
    const char *type_name;     /* e.g. "CYBERLOAD F3", "PAUSE" */
    int is_data;               /* 1 = recognized data block (not pause/gap) */
    int has_checksum;          /* format carries a checksum */
    int checksum_ok;           /* 1 = checkbyte verified (when has_checksum) */
    int read_errors;
    int start_addr;            /* C64 memory range of the decoded data */
    int end_addr;
    int size;                  /* decoded byte count */
    int tap_start;             /* TAP file offset of first pulse */
    int tap_end;               /* TAP file offset of last pulse */
    const char *name;          /* ASCII filename or "" */
    const unsigned char *data; /* decoded bytes (engine-owned) or NULL */
} tapclean_block_t;

/* One loadable program (blocks united per the engine's PRG database) */
typedef struct {
    int type;                  /* loader type id of the (last united) block */
    const char *type_name;
    int is_cbm_header;         /* 1 = C64 ROM-TAPE HEADER block (not a program) */
    int start_addr;
    int end_addr;
    int size;
    const char *name;          /* ASCII filename or "" */
    const unsigned char *data; /* decoded bytes (engine-owned) */
    int read_errors;
    int tap_start;             /* TAP offset of the first contributing block */
    int tap_end;               /* TAP offset of the last contributing block */
} tapclean_prg_t;

/* Allocate work buffers (PSRAM-first on ESP32) and the block database.
   Returns 1 on success. Idempotent. */
int tapclean_init(void);

/* Free everything, including a loaded tape. */
void tapclean_shutdown(void);

/* Load a TAP (or DC2N DMP, auto-detected) image for analysis.
   Takes OWNERSHIP of 'buf' (must come from malloc; the engine frees it).
   Returns 1 on success. */
int tapclean_load_buffer(unsigned char *buf, unsigned int len,
                         int machine, int is_ntsc);

/* Scan the loaded tape with every enabled scanner and build the block and
   PRG databases. unite_blocks: join neighbouring/contiguous blocks into
   single loadable programs (recommended for turbo loaders that split a
   game into hundreds of chunks). Returns the number of PRGs found, or -1
   if the image is not a valid tape. */
int tapclean_analyze_tap(int unite_blocks);

/* Result accessors - valid until the next load/unload/shutdown */
int tapclean_block_count(void);
int tapclean_get_block(int idx, tapclean_block_t *out);
int tapclean_prg_count(void);
int tapclean_get_prg(int idx, tapclean_prg_t *out);

/* Scan summary */
int tapclean_detected_percent(void);
unsigned int tapclean_tap_time_ms(void);      /* whole-tape play time */
unsigned int tapclean_time_at_ms(int tap_offset); /* play time 20..offset */
int tapclean_offset_at_ms(unsigned int ms);   /* inverse of the above */
unsigned int tapclean_duration_ms(int p1, int p2); /* play time p1..p2 */

/* Free the loaded tape image and reset the databases (keeps work buffers) */
void tapclean_unload(void);

#ifdef __cplusplus
}
#endif

#endif /* TAPCLEAN_API_H */
