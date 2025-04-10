#include <string>

#ifdef BOARD_HAS_PSRAM
#include <esp_psram.h>
#ifdef CONFIG_IDF_TARGET_ESP32
#include <esp32/himem.h>
#endif
#endif

#include "../include/debug.h"

void testHeader(std::string testName);
void runTestsSuite();
void lfs_test( void );

// #include <archive.h>
// #include <archive_entry.h>

// void la_test( void ) {

//     struct archive *a;
//     struct archive_entry *entry;
//     int r;

//     a = archive_read_new();
//     archive_read_support_filter_all(a);
//     archive_read_support_format_all(a);
//     r = archive_read_open_filename(a, "archive.tar", 10240); // Note 1
//     if (r != ARCHIVE_OK)
//     exit(1);
//     while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
//     printf("%s\r\n",archive_entry_pathname(entry));
//     archive_read_data_skip(a);  // Note 2
//     }
//     r = archive_read_free(a);  // Note 3
//     if (r != ARCHIVE_OK)
//     exit(1);
// }

class LeakDetector {
    uint32_t prevMem = 0;
    uint32_t startMem = 0;
    std::string tag;

public:
    LeakDetector(std::string t) {
        prevMem = esp_get_free_internal_heap_size();
        startMem = prevMem;
        tag = t;
    };

    void check(std::string checkpoint) {
        uint32_t memNow = esp_get_free_internal_heap_size();
        if(memNow < prevMem) {
            Debug_printf("[I:ALLOC:%s] %lu bytes allocated at checkpoint: %s\r\n", tag.c_str(), memNow-prevMem, checkpoint.c_str());
        }
        else if(memNow > prevMem) {
            Debug_printf("[I:FREE :%s] %lu bytes freed at checkpoint: %s\r\n", tag.c_str(), prevMem-memNow, checkpoint.c_str());
        }
        else {
            Debug_printf("[I:NONE :%s] no change at checkpoint: %s\r\n", tag.c_str(), checkpoint.c_str());
        }

        prevMem = memNow;
    }

    void finish() {
        uint32_t memNow = esp_get_free_internal_heap_size();
        if(memNow < startMem) {
            Debug_printf("[E:LEAK:%s] sorry to say, but it leaked: %lu bytes\r\n", tag.c_str(), memNow-startMem);
        }
    }
};