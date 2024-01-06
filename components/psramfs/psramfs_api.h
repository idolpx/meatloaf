/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "psram.h"
#include "esp_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_PSRAMFS_PATH_MAX 15

/**
 * @brief PSRAMFS definition structure
 */
typedef struct {
    psram *fs;                             /*!< Handle to the underlying PSRAMFS */
    SemaphoreHandle_t lock;                 /*!< FS lock */
    const esp_partition_t* partition;       /*!< The partition on which PSRAMFS is located */
    char base_path[ESP_PSRAMFS_PATH_MAX+1];  /*!< Mount point */
    bool by_label;                          /*!< Partition was mounted by label */
    psramfs_config cfg;                      /*!< PSRAMFS Mount configuration */
    uint8_t *work;                          /*!< Work Buffer */
    uint8_t *fds;                           /*!< File Descriptor Buffer */
    uint32_t fds_sz;                        /*!< File Descriptor Buffer Length */
    uint8_t *cache;                         /*!< Cache Buffer */
    uint32_t cache_sz;                      /*!< Cache Buffer Length */
} esp_psramfs_t;

s32_t psramfs_api_read(psram *fs, uint32_t addr, uint32_t size, uint8_t *dst);

s32_t psramfs_api_write(psram *fs, uint32_t addr, uint32_t size, uint8_t *src);

s32_t psramfs_api_erase(psram *fs, uint32_t addr, uint32_t size);

void psramfs_api_check(psram *fs, psramfs_check_type type,
                            psramfs_check_report report, uint32_t arg1, uint32_t arg2);

#ifdef __cplusplus
}
#endif
