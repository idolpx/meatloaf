/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_psramfs.h"
#include "psramfs_api.h"

static const char* TAG = "PSRAMFS";

void psramfs_api_lock(psram *fs)
{
    (void) xSemaphoreTake(((esp_psramfs_t *)(fs->user_data))->lock, portMAX_DELAY);
}

void psramfs_api_unlock(psram *fs)
{
    xSemaphoreGive(((esp_psramfs_t *)(fs->user_data))->lock);
}

s32_t psramfs_api_read(psram *fs, uint32_t addr, uint32_t size, uint8_t *dst)
{
    esp_err_t err = esp_partition_read(((esp_psramfs_t *)(fs->user_data))->partition,
                                        addr, dst, size);
    if (unlikely(err)) {
        ESP_LOGE(TAG, "failed to read addr 0x%08" PRIx32 ", size 0x%08" PRIx32 ", err %d", addr, size, err);
        return -1;
    }
    return 0;
}

s32_t psramfs_api_write(psram *fs, uint32_t addr, uint32_t size, uint8_t *src)
{
    esp_err_t err = esp_partition_write(((esp_psramfs_t *)(fs->user_data))->partition,
                                        addr, src, size);
    if (unlikely(err)) {
        ESP_LOGE(TAG, "failed to write addr 0x%08" PRIx32 ", size 0x%08" PRIx32 ", err %d", addr, size, err);
        return -1;
    }
    return 0;
}

s32_t psramfs_api_erase(psram *fs, uint32_t addr, uint32_t size)
{
    esp_err_t err = esp_partition_erase_range(((esp_psramfs_t *)(fs->user_data))->partition,
                                        addr, size);
    if (err) {
        ESP_LOGE(TAG, "failed to erase addr 0x%08" PRIx32 ", size 0x%08" PRIx32 ", err %d", addr, size, err);
        return -1;
    }
    return 0;
}

void psramfs_api_check(psram *fs, psramfs_check_type type,
                            psramfs_check_report report, uint32_t arg1, uint32_t arg2)
{
    static const char * psramfs_check_type_str[3] = {
        "LOOKUP",
        "INDEX",
        "PAGE"
    };

    static const char * psramfs_check_report_str[7] = {
        "PROGRESS",
        "ERROR",
        "FIX INDEX",
        "FIX LOOKUP",
        "DELETE ORPHANED INDEX",
        "DELETE PAGE",
        "DELETE BAD FILE"
    };

    if (report != PSRAMFS_CHECK_PROGRESS) {
        ESP_LOGE(TAG, "CHECK: type:%s, report:%s, %" PRIx32 ":%" PRIx32, psramfs_check_type_str[type],
                              psramfs_check_report_str[report], arg1, arg2);
    } else {
        ESP_LOGV(TAG, "CHECK PROGRESS: report:%s, %" PRIx32 ":%" PRIx32,
                              psramfs_check_report_str[report], arg1, arg2);
    }
}
