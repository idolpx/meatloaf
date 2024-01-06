/*
 * SPDX-FileCopyrightText: 2016-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>

#include "Mockqueue.h"

#include "esp_partition.h"
#include "psram.h"
#include "psramfs_nucleus.h"
#include "psramfs_api.h"

#include "unity.h"
#include "unity_fixture.h"

TEST_GROUP(psram);

TEST_SETUP(psram)
{
    // CMock init for psram xSemaphore* use
    xQueueSemaphoreTake_IgnoreAndReturn(0);
    xQueueGenericSend_IgnoreAndReturn(0);
}

TEST_TEAR_DOWN(psram)
{
}

static void init_psramfs(psram *fs, uint32_t max_files)
{
    psramfs_config cfg = {};
    s32_t psramfs_res;
    u32_t flash_sector_size;

    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "storage");
    TEST_ASSERT_NOT_NULL(partition);

    // Configure objects needed by PSRAMFS
    esp_psramfs_t *user_data = (esp_psramfs_t *) calloc(1, sizeof(*user_data));
    user_data->partition = partition;
    fs->user_data = (void *)user_data;

    flash_sector_size = 4096;

    cfg.hal_erase_f = psramfs_api_erase;
    cfg.hal_read_f = psramfs_api_read;
    cfg.hal_write_f = psramfs_api_write;
    cfg.log_block_size = flash_sector_size;
    cfg.log_page_size = CONFIG_PSRAMFS_PAGE_SIZE;
    cfg.phys_addr = 0;
    cfg.phys_erase_block = flash_sector_size;
    cfg.phys_size = partition->size;

    uint32_t work_sz = cfg.log_page_size * 2;
    uint8_t *work = (uint8_t *) malloc(work_sz);

    uint32_t fds_sz = max_files * sizeof(psramfs_fd);
    uint8_t *fds = (uint8_t *) malloc(fds_sz);

#if CONFIG_PSRAMFS_CACHE
    uint32_t cache_sz = sizeof(psramfs_cache) + max_files * (sizeof(psramfs_cache_page)
                        + cfg.log_page_size);
    uint8_t *cache = (uint8_t *) malloc(cache_sz);
#else
    uint32_t cache_sz = 0;
    uint8_t cache = NULL;
#endif

    // Special mounting procedure: mount, format, mount as per
    // https://github.com/pellepl/psram/wiki/Using-psram
    psramfs_res = PSRAMFS_mount(fs, &cfg, work, fds, fds_sz,
                              cache, cache_sz, psramfs_api_check);

    if (psramfs_res == PSRAMFS_ERR_NOT_A_FS) {
        psramfs_res = PSRAMFS_format(fs);
        TEST_ASSERT_TRUE(psramfs_res >= PSRAMFS_OK);

        psramfs_res = PSRAMFS_mount(fs, &cfg, work, fds, fds_sz,
                                  cache, cache_sz, psramfs_api_check);
    }

    TEST_ASSERT_TRUE(psramfs_res >= PSRAMFS_OK);
}

static void deinit_psramfs(psram *fs)
{
    PSRAMFS_unmount(fs);

    free(fs->work);
    free(fs->user_data);
    free(fs->fd_space);

#if CONFIG_PSRAMFS_CACHE
    free(fs->cache);
#endif
}

static void check_psramfs_files(psram *fs, const char *base_path, char *cur_path)
{
    DIR *dir;
    struct dirent *entry;
    size_t len = strlen(cur_path);

    if (len == 0) {
        strcpy(cur_path, base_path);
        len = strlen(base_path);
    }

    dir = opendir(cur_path);
    TEST_ASSERT_TRUE(dir != 0);

    while ((entry = readdir(dir)) != NULL) {
        char *name = entry->d_name;

        char path[PATH_MAX] = { 0 };

        // Read the file from host FS
        strcpy(path, cur_path);
        strcat(path, "/");
        strcat(path, name);

        struct stat sb;
        stat(path, &sb);

        if (S_ISDIR(sb.st_mode)) {
            if (!strcmp(name, ".") || !strcmp(name, "..")) {
                continue;
            }
            cur_path[len] = '/';
            strcpy(cur_path + len + 1, name);
            check_psramfs_files(fs, base_path, cur_path);
            cur_path[len] = '\0';
        } else {
            FILE *f = fopen(path, "r");
            TEST_ASSERT_NOT_NULL(f);
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);

            char *f_contents = (char *) malloc(sz);
            fread(f_contents, 1, sz, f);
            fclose(f);

            s32_t psramfs_res;

            // Read the file from PSRAMFS
            char *psramfs_path = path + strlen(base_path);
            psramfs_res = PSRAMFS_open(fs, psramfs_path, PSRAMFS_RDONLY, 0);

            TEST_ASSERT_TRUE(psramfs_res > PSRAMFS_OK);

            psramfs_file fd = psramfs_res;

            psramfs_stat stat;
            psramfs_res = PSRAMFS_stat(fs, psramfs_path, &stat);

            char *psramfs_f_contents = (char *) malloc(stat.size);
            psramfs_res = PSRAMFS_read(fs, fd, psramfs_f_contents, stat.size);
            TEST_ASSERT_TRUE(psramfs_res == stat.size);

            // Compare the contents
            TEST_ASSERT_TRUE(sz == stat.size);

            bool same = memcmp(f_contents, psramfs_f_contents, sz) == 0;
            TEST_ASSERT_TRUE(same);

            free(f_contents);
            free(psramfs_f_contents);
        }
    }
    closedir(dir);
}

TEST(psram, format_disk_open_file_write_and_read_file)
{
    psram fs;
    s32_t psramfs_res;

    init_psramfs(&fs, 5);

    // Open test file
    psramfs_res = PSRAMFS_open(&fs, "test.txt", PSRAMFS_O_CREAT | PSRAMFS_O_RDWR, 0);
    TEST_ASSERT_TRUE(psramfs_res >= PSRAMFS_OK);

    // Generate data
    psramfs_file file = psramfs_res;

    uint32_t data_size = 100000;

    char *data = (char *) malloc(data_size);
    char *read = (char *) malloc(data_size);

    for (uint32_t i = 0; i < data_size; i += sizeof(i)) {
        *((uint32_t *)(data + i)) = i;
    }

    // Write data to file
    psramfs_res = PSRAMFS_write(&fs, file, (void *)data, data_size);
    TEST_ASSERT_TRUE(psramfs_res >= PSRAMFS_OK);
    TEST_ASSERT_TRUE(psramfs_res == data_size);

    // Set the file object pointer to the beginning
    psramfs_res = PSRAMFS_lseek(&fs, file, 0, PSRAMFS_SEEK_SET);
    TEST_ASSERT_TRUE(psramfs_res >= PSRAMFS_OK);

    // Read the file
    psramfs_res = PSRAMFS_read(&fs, file, (void *)read, data_size);
    TEST_ASSERT_TRUE(psramfs_res >= PSRAMFS_OK);
    TEST_ASSERT_TRUE(psramfs_res == data_size);

    // Close the test file
    psramfs_res = PSRAMFS_close(&fs, file);
    TEST_ASSERT_TRUE(psramfs_res >= PSRAMFS_OK);

    TEST_ASSERT_TRUE(memcmp(data, read, data_size) == 0);

    deinit_psramfs(&fs);

    free(read);
    free(data);
}

TEST(psram, can_read_psramfs_image)
{
    psram fs;
    s32_t psramfs_res;

    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "storage");

    // Write the contents of the image file to partition
    FILE *img_file = fopen("image.bin", "r");
    TEST_ASSERT_NOT_NULL(img_file);

    fseek(img_file, 0, SEEK_END);
    long img_size = ftell(img_file);
    fseek(img_file, 0, SEEK_SET);

    char *img = (char *) malloc(img_size);
    fread(img, 1, img_size, img_file);
    fclose(img_file);

    TEST_ASSERT_TRUE(partition->size == img_size);

    esp_partition_erase_range(partition, 0, partition->size);
    esp_partition_write(partition, 0, img, img_size);

    free(img);

    // Mount the psram partition and init filesystem, using the contents of
    // the image file
    init_psramfs(&fs, 1024);

    // Check psram consistency
    psramfs_res = PSRAMFS_check(&fs);
    TEST_ASSERT_TRUE(psramfs_res == PSRAMFS_OK);

    char path_buf[PATH_MAX];

    // The image is created from the psram source directory. Compare the files in that
    // directory to the files read from the PSRAMFS image.
    check_psramfs_files(&fs, "../psram", path_buf);

    deinit_psramfs(&fs);
}

TEST_GROUP_RUNNER(psram)
{
    RUN_TEST_CASE(psram, format_disk_open_file_write_and_read_file);
    RUN_TEST_CASE(psram, can_read_psramfs_image);
}

static void run_all_tests(void)
{
    RUN_TEST_GROUP(psram);
}

int main(int argc, char **argv)
{
    UNITY_MAIN_FUNC(run_all_tests);
    return 0;
}
