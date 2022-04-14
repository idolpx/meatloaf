
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>

#include "esp_littlefs.h"
#include <dirent.h>

#include "ml_tests.h"

static const char *TAG = "demo_esp_littlefs";

void lfs_test ( void )
{
        printf("Demo LittleFs implementation by esp_littlefs!\n");
        printf("   https://github.com/joltwallet/esp_littlefs\n");

        /* Print chip information */
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        printf("This is %s chip with %d CPU cores, WiFi%s%s, ",
               CONFIG_IDF_TARGET,
               chip_info.cores,
               (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
               (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

        printf("silicon revision %d, ", chip_info.revision);

        printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
               (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

        printf("Free heap: %d\n", esp_get_free_heap_size());

        printf("Now we are starting the LittleFs Demo ...\n");


        esp_vfs_littlefs_conf_t conf = {
            .base_path = "/flashfs",
            .partition_label = "flashfs",
            .format_if_mount_failed = true,
            .dont_mount = false,
        };

        // // Use settings defined above to initialize and mount LittleFS filesystem.
        // // Note: esp_vfs_littlefs_register is an all-in-one convenience function.
        // esp_err_t ret = esp_vfs_littlefs_register(&conf);

        // if (ret != ESP_OK)
        // {
        //         if (ret == ESP_FAIL)
        //         {
        //                 ESP_LOGE(TAG, "Failed to mount or format filesystem");
        //         }
        //         else if (ret == ESP_ERR_NOT_FOUND)
        //         {
        //                 ESP_LOGE(TAG, "Failed to find LittleFS partition");
        //         }
        //         else
        //         {
        //                 ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        //         }
        //         return;
        // }

        size_t total = 0, used = 0;
        esp_err_t ret = esp_littlefs_info(conf.partition_label, &total, &used);
        if (ret != ESP_OK)
        {
                printf("Failed to get LittleFS partition information (%s)\n", esp_err_to_name(ret));
        }
        else
        {
                printf("Partition size: total: %d, used: %d\n", total, used);
        }

        // Use POSIX and C standard library functions to work with files.
        // First create a file.
        printf("Opening file\n");
        FILE *f = fopen("/flashfs/hello.txt", "w");
        if (f == NULL)
        {
                printf("Failed to open file for writing\n");
                return;
        }
        fprintf(f, "LittleFS Rocks!\n");
        fclose(f);
        printf("File written\n");

        // Check if destination file exists before renaming
        struct stat st;
        if (stat("/flashfs/foo.txt", &st) == 0)
        {
                // Delete it if it exists
                unlink("/flashfs/foo.txt");
        }

        // Rename original file
        printf("Renaming file\n");
        if (rename("/flashfs/hello.txt", "/flashfs/foo.txt") != 0)
        {
                printf("Rename failed\n");
                return;
        }

        // Open renamed file for reading
        printf("Reading file\n");
        f = fopen("/flashfs/foo.txt", "r");
        if (f == NULL)
        {
                printf("Failed to open file for reading\n");
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
        printf("Read from file: '%s'\n", line);


        // open directory path up 
        DIR* path = opendir("/flashfs/");

        // check to see if opening up directory was successful
        if(path != NULL)
        {
            // stores underlying info of files and sub_directories of directory_path
            struct dirent* underlying_file = NULL;

            // iterate through all of the  underlying files of directory_path
            while((underlying_file = readdir(path)) != NULL)
            {
                printf("%s\n", underlying_file->d_name);
            }
            
            closedir(path);
        }



        // // All done, unmount partition and disable LittleFS
        // esp_vfs_littlefs_unregister(conf.partition_label);
        // ESP_LOGI(TAG, "LittleFS unmounted");
}