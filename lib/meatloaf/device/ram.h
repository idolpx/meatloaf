/* Himem API example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef MEATLOAF_HIMEMFS_H
#define MEATLOAF_HIMEMFS_H

#ifndef CONFIG_IDF_TARGET_ESP32S3

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "dirent.h"
#include "FS.h"

#ifdef BOARD_HAS_PSRAM
#ifdef CONFIG_IDF_TARGET_ESP32S3
#include <esp_psram.h>
#else
#include <esp32/himem.h>
#endif
#endif

#include <cstdint>
#include <string>
#include <cstring>
#include <unordered_map>

#include "meatloaf.h"
#include "meat_buffer.h"

class HighMemory 
{
    private:
    esp_himem_handle_t mh;      // Handle for the address space we're using
    esp_himem_rangehandle_t rh; // Handle for the actual RAM.
    uint32_t *mem_ptr;          // Memory pointer

    protected:

    public:
    //Fill memory with pseudo-random data generated from the given seed.
    //Fills the memory in 32-bit words for speed.
    static void fill_mem_seed(int seed, void *mem, int len);

    //Check the memory filled by fill_mem_seed. Returns true if the data matches the data
    //that fill_mem_seed wrote (when given the same seed).
    //Returns true if there's a match, false when the region differs from what should be there.
    static bool check_mem_seed(int seed, void *mem, int len, int phys_addr);

    //Allocate a himem region, fill it with data, check it and release it.
    static bool test_region(int check_size, int seed);

    size_t size() {
        return esp_himem_get_phys_size();
    }

    size_t free() {
        return esp_himem_get_free_size();
    }

    size_t blocks() {
        return ESP_HIMEM_BLKSZ;
    }

    size_t block_size() {
        return ESP_HIMEM_BLKSZ;
    }

    bool open(int block) {
        esp_err_t r = esp_himem_map(mh, rh, block, 0, ESP_HIMEM_BLKSZ, 0, (void**)&mem_ptr);
        return r;
    }

    std::string read();
    std::string write();

    bool seek();
};

// void app_main(void)
// {
//     RAMMFileSystem hm;
    
//     size_t memcnt = hm.size();
//     size_t memfree = hm.free();

//     printf("Himem has %dKiB of memory, %dKiB of which is free. Testing the free memory...\n", (int)memcnt/1024, (int)memfree/1024);
//     assert(test_region(memfree, 0xaaaa));
//     printf("Done!\n");
// }


/********************************************************
 * HighMemoryHandle
 ********************************************************/

class HighMemoryHandle {
public:
    //int rc;
    FILE* file_h = nullptr;

    HighMemoryHandle() 
    {
        //Debug_printv("*** Creating flash handle");
        memset(&file_h, 0, sizeof(file_h));
    };
    ~HighMemoryHandle();
    void obtain(std::string localPath, std::string mode);
    void dispose();

private:
    int flags = 0;
};


/********************************************************
 * MFile
 ********************************************************/

class RAMMFile: public MFile
{

public:
    std::string basepath = "";
    
    RAMMFile(std::string path): MFile(path) {
        // parseUrl( path );

        // Find full filename for wildcard
        if (mstr::contains(name, "?") || mstr::contains(name, "*"))
            readEntry( name );

        if (!pathValid(path.c_str()))
            m_isNull = true;
        else
            m_isNull = false;

        isWritable = true;
        //Debug_printv("basepath[%s] path[%s] valid[%d]", basepath.c_str(), this->path.c_str(), m_isNull);
    };
    ~RAMMFile() {
        //printf("*** Destroying flashfile %s\r\n", url.c_str());
        closeDir();
    }

    //MFile* cd(std::string newDir);
    bool isDirectory() override;
    MStream* getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override ; // has to return OPENED stream
    MStream* getDecodedStream(std::shared_ptr<MStream> src);

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override;
    bool exists() override;
    bool remove() override;
    bool rename(std::string dest);

    time_t getLastWrite() override;
    time_t getCreationTime() override;

    bool readEntry( std::string filename );

protected:
    DIR* dir;
    bool dirOpened = false;

private:
    virtual void openDir(std::string path);
    virtual void closeDir();

    bool _valid;
    std::string _pattern;

    bool pathValid(std::string path);
};


/********************************************************
 * MFileSystem
 ********************************************************/

class RAMMFileSystem: public MFileSystem 
{
public:
    RAMMFileSystem() : MFileSystem("ram") {};

    bool handles(std::string path) override
    {
        std::string pattern = "ram:";
        return mstr::equals(path, pattern, false);
    }

    MFile* getFile(std::string path) override
    {
        //Debug_printv("path[%s]", path.c_str());
        return new RAMMFile(path);
    }
};


#endif
#endif // MEATLOAF_HIMEM_FS