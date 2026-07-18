/*
 * tapclean_alloc.c - PSRAM-first allocator for the TAPClean engine.
 *
 * mydefs.h redirects the engine's malloc() here (TAPCLEAN_EMBEDDED): the
 * scan allocates thousands of small blk_t/dd buffers, and on ESP32 plain
 * malloc() places small allocations in internal DRAM, which the engine
 * would exhaust. free() works unchanged on these allocations (unified
 * IDF heap). This file must NOT include mydefs.h.
 */

#include <stdlib.h>
#include <stdio.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void *tapclean_psram_malloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL)
        p = malloc(n);
    return p;
}

/* Called from the pulse readers every ~64K reads: emits a progress dot
   so a long scanner pass shows life, and every 4th call lets lower-
   priority tasks and the idle task (task watchdog) run */
void tapclean_scan_yield(void)
{
    static unsigned int n;

    putchar('.');
    fflush(stdout);
    if ((++n & 3) == 0)
        vTaskDelay(1);
}
#else
void *tapclean_psram_malloc(size_t n) { return malloc(n); }
void tapclean_scan_yield(void) {}
#endif
