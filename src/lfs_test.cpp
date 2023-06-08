
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>

#include "esp_flash.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_chip_info.h"
#include "spi_flash_mmap.h"
#endif

#include "esp_littlefs.h"
#include <dirent.h>

#include "ml_tests.h"


void lfs_test ( void )
{
        printf("Demo LittleFs implementation by esp_littlefs!\r\n");
        printf("   https://github.com/joltwallet/esp_littlefs\r\n");

        /* Print chip information */
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        printf("This is %s chip with %d CPU cores, WiFi%s%s, ",
               CONFIG_IDF_TARGET,
               chip_info.cores,
               (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
               (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

        printf("silicon revision %d, ", chip_info.revision);

        uint32_t size_flash_chip = 0;
        printf("%dMB %s flash\r\n", (esp_flash_get_size(NULL, &size_flash_chip) / (1024 * 1024)),
               (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

        //printf("Free heap: %lu\r\n", esp_get_free_heap_size());

        printf("Now we are starting the LittleFs Demo ...\r\n");


        esp_vfs_littlefs_conf_t conf = {
            .base_path = "/flash",
            .partition_label = "flash",
            .format_if_mount_failed = true,
            .dont_mount = false,
        };


        size_t total = 0, used = 0;
        esp_err_t ret = esp_littlefs_info(conf.partition_label, &total, &used);
        if (ret != ESP_OK)
        {
                printf("Failed to get LittleFS partition information (%s)\r\n", esp_err_to_name(ret));
        }
        else
        {
                printf("Partition size: total: %d, used: %d\r\n", total, used);
        }

        // Use POSIX and C standard library functions to work with files.
        // First create a file.
        printf("Opening file\r\n");
        FILE *f = fopen("/flash/hello.txt", "w");
        if (f == NULL)
        {
                printf("Failed to open file for writing\r\n");
                return;
        }
        fprintf(f, "LittleFS Rocks!\r\n");
        fclose(f);
        printf("File written\r\n");

        // Check if destination file exists before renaming
        struct stat st;
        if (stat("/flash/foo.txt", &st) == 0)
        {
                // Delete it if it exists
                unlink("/flash/foo.txt");
        }

        // Rename original file
        printf("Renaming file\r\n");
        if (rename("/flash/hello.txt", "/flash/foo.txt") != 0)
        {
                printf("Rename failed\r\n");
                return;
        }

        // Open renamed file for reading
        printf("Reading file\r\n");
        f = fopen("/flash/foo.txt", "r");
        if (f == NULL)
        {
                printf("Failed to open file for reading\r\n");
                return;
        }
        char line[64];
        fgets(line, sizeof(line), f);
        fclose(f);
        // strip newline
        char *pos = strchr(line, '\n');
        if (pos)
        {
                *pos = '\0';
        }
        printf("Read from file: '%s'\r\n", line);


        // open directory path up 
        DIR* path = opendir("/flash/");

        // check to see if opening up directory was successful
        if(path != NULL)
        {
            // stores underlying info of files and sub_directories of directory_path
            struct dirent* underlying_file = NULL;

            // iterate through all of the  underlying files of directory_path
            while((underlying_file = readdir(path)) != NULL)
            {
                printf("%s\r\n", underlying_file->d_name);
            }
            
            closedir(path);
        }



        // // All done, unmount partition and disable LittleFS
        // esp_vfs_littlefs_unregister(conf.partition_label);
        // ESP_LOGI(TAG, "LittleFS unmounted");
}