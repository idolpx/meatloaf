/*
 * tapclean_api.c - embedded library wrapper around the vendored TAPClean
 * engine (see include/tapclean_api.h). GPL v2 or later.
 */

#include <stdlib.h>
#include <string.h>

#include "tapclean_api.h"

#include "src/mydefs.h"
#include "src/main.h"
#include "src/database.h"
#include "src/crc32.h"

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
static void *big_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL)
        p = malloc(n);
    return p;
}
#else
static void *big_alloc(size_t n) { return malloc(n); }
#endif

/* Sizes for the (former) static work buffers. 'info' only ever holds one
   block's description at a time in the embedded build (reset per block in
   describe_blocks), so it can be much smaller than upstream's 1 MB. */
#define TC_INFO_SIZE (64 * 1024)
#define TC_LIN_SIZE  (64 * 1024)
#define TC_CBM_SIZE  (64 * 1024)

/* glue in main.c (embedded section) */
extern int tapclean_embedded_load(unsigned char *buf, unsigned int len);
extern void tapclean_embedded_unload(void);
extern float tapclean_embedded_duration(int p1, int p2);
extern int tapclean_embedded_offset_at_ms(unsigned int ms);

/* main.c globals that have no extern declaration in mydefs.h */
extern char *tmp;
extern char pal;
extern char ntsc;
extern long cps;

static int tc_initialized = 0;

int tapclean_init(void)
{
    if (tc_initialized)
        return 1;

    info = (char *)big_alloc(TC_INFO_SIZE);
    lin = (char *)big_alloc(TC_LIN_SIZE);
    tmp = (char *)big_alloc(TC_LIN_SIZE);
    cbm_program = (unsigned char *)big_alloc(TC_CBM_SIZE);
    prg = (struct prg_t *)big_alloc(BLKMAX * sizeof(struct prg_t));

    if (!info || !lin || !tmp || !cbm_program || !prg) {
        tapclean_shutdown();
        return 0;
    }

    info[0] = '\0';
    lin[0] = '\0';
    tmp[0] = '\0';
    memset(prg, 0, BLKMAX * sizeof(struct prg_t));

    if (!database_create_blk_db()) {
        tapclean_shutdown();
        return 0;
    }

    crc32_build_crc_table();

    quiet = TRUE;  /* no per-scanner console chatter on the device */

    tc_initialized = 1;
    return 1;
}

void tapclean_shutdown(void)
{
    if (tc_initialized) {
        tapclean_embedded_unload();
        database_reset_prg_db();
        database_destroy_blk_db();
        crc32_free_crc_table();
    }

    free(info);      info = NULL;
    free(lin);       lin = NULL;
    free(tmp);       tmp = NULL;
    free(cbm_program); cbm_program = NULL;
    free(prg);       prg = NULL;

    tc_initialized = 0;
}

int tapclean_load_buffer(unsigned char *buf, unsigned int len,
                         int machine, int is_ntsc)
{
    if (!tapclean_init()) {
        free(buf);
        return 0;
    }

    database_reset_prg_db();

    c64 = (machine == TAPCLEAN_MACHINE_C64);
    c20 = (machine == TAPCLEAN_MACHINE_VIC20);
    c16 = (machine == TAPCLEAN_MACHINE_C16);
    pal = !is_ntsc;
    ntsc = is_ntsc ? TRUE : FALSE;

    if (c16)
        cps = is_ntsc ? C16_NTSC_CPS : C16_PAL_CPS;
    else if (c20)
        cps = is_ntsc ? VIC20_NTSC_CPS : VIC20_PAL_CPS;
    else
        cps = is_ntsc ? C64_NTSC_CPS : C64_PAL_CPS;

    return tapclean_embedded_load(buf, len);
}

int tapclean_analyze_tap(int unite_blocks)
{
    if (!tc_initialized || tap.tmem == NULL)
        return -1;

    prgunite = unite_blocks ? TRUE : FALSE;

    if (!analyze())
        return -1;

    database_make_prg_db();

    return tapclean_prg_count();
}

int tapclean_block_count(void)
{
    int i;

    if (!tc_initialized)
        return 0;
    for (i = 0; i < BLKMAX && blk[i]->lt != LT_NONE; i++)
        ;
    return i;
}

int tapclean_get_block(int idx, tapclean_block_t *out)
{
    struct blk_t *b;

    if (!tc_initialized || idx < 0 || idx >= tapclean_block_count())
        return 0;

    b = blk[idx];
    out->type = b->lt;
    out->type_name = ft[b->lt].name;
    out->is_data = (b->lt > PAUSE);
    out->has_checksum = (ft[b->lt].has_cs == CSYES && b->cs_exp != HASNOTCHECKSUM);
    out->checksum_ok = (out->has_checksum && b->cs_exp == b->cs_act);
    out->read_errors = b->rd_err;
    out->start_addr = b->cs;
    out->end_addr = b->ce;
    out->size = b->cx;
    out->tap_start = b->p1;
    out->tap_end = b->p4;
    out->name = b->fn ? b->fn : "";
    out->data = b->dd;
    return 1;
}

int tapclean_prg_count(void)
{
    int i;

    if (!tc_initialized || prg == NULL)
        return 0;
    for (i = 0; i < BLKMAX && prg[i].lt != 0; i++)
        ;
    return i;
}

int tapclean_get_prg(int idx, tapclean_prg_t *out)
{
    struct prg_t *p;

    if (!tc_initialized || idx < 0 || idx >= tapclean_prg_count())
        return 0;

    p = &prg[idx];
    out->type = p->lt;
    out->type_name = ft[p->lt].name;
    out->is_cbm_header = (p->lt == CBM_HEAD);
    out->start_addr = p->cs;
    out->end_addr = p->ce;
    out->size = p->cx;
    /* prefer the first block's raw name over prg fn, which has been
       filesystem-mangled by fname_text() (spaces become underscores) */
    if (blk[p->blkidstart]->fn && blk[p->blkidstart]->fn[0])
        out->name = blk[p->blkidstart]->fn;
    else
        out->name = p->fn ? p->fn : "";
    out->data = p->dd;
    out->read_errors = p->errors;
    out->tap_start = blk[p->blkidstart]->p1;
    out->tap_end = blk[p->blkidend]->p4;
    return 1;
}

int tapclean_detected_percent(void)
{
    return tc_initialized ? tap.detected_percent : 0;
}

unsigned int tapclean_tap_time_ms(void)
{
    return tc_initialized ? (unsigned int)(tap.taptime * 1000.0f) : 0;
}

unsigned int tapclean_time_at_ms(int tap_offset)
{
    if (!tc_initialized || tap.tmem == NULL)
        return 0;
    return (unsigned int)(tapclean_embedded_duration(20, tap_offset) * 1000.0f);
}

unsigned int tapclean_duration_ms(int p1, int p2)
{
    if (!tc_initialized || tap.tmem == NULL)
        return 0;
    return (unsigned int)(tapclean_embedded_duration(p1, p2) * 1000.0f);
}

int tapclean_offset_at_ms(unsigned int ms)
{
    if (!tc_initialized)
        return 0;
    return tapclean_embedded_offset_at_ms(ms);
}

void tapclean_unload(void)
{
    if (!tc_initialized)
        return;
    tapclean_embedded_unload();
    database_reset_prg_db();
}
