#include "VFSCommands.h"

#include <cstring>
#include <cerrno>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "esp_littlefs.h"
#include "esp_vfs_fat.h"
#include <sys/syslimits.h>
#include <iostream>
#include <esp_heap_caps.h>
#include <zlib.h>
#include "../../meatloaf/network/http.h"
#ifndef MIN_CONFIG
#include <archive.h>
#include <archive_entry.h>
#endif

static inline void *psram_malloc(size_t sz) {
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(sz);
}

#include "fsFlash.h"
#include "fnFsSD.h"
#include "fnConfig.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "../Console.h"
#include "../Helpers/PWDHelpers.h"
#include "../ute/ute.h"
#include "../../device/iec/meatloaf.h"
#include "mlff.h"

using namespace ESP32Console;

int cat(int argc, char **argv)
{
    if (argc == 1)
    {
        Serial.printf("You have to pass at least one file path!\r\n");
        return EXIT_SUCCESS;
    }

    for (int n = 1; n < argc; n++)
    {
        std::unique_ptr<MFile> path(getCurrentPath()->cd(argv[n]));
        Meat::iostream istream(path.get());

        if(istream.is_open()) {
            if(istream.eof()) {
                Serial.print("Stream returned EOF!");
            } else {    
                while(!istream.eof()) {
                    char chr = istream.get();
                    if(!istream.eof())
                        Serial.printf("%c", chr);
                }
            }
            istream.close();
        }
        else {
            Serial.printf("ERROR:%s could not be read!\r\n", path->url.c_str());
        }
    }

    return EXIT_SUCCESS;
}

int hex(int argc, char **argv)
{
    if (argc == 1)
    {
        Serial.printf("You have to pass at least one file path!\r\n");
        return EXIT_SUCCESS;
    }

    for (int n = 1; n < argc; n++)
    {
        std::unique_ptr<MFile> path(getCurrentPath()->cd(argv[n]));
        //Debug_printv("Opening file for hex: %s", path->url.c_str());
        Meat::iostream istream(path.get());

        if(istream.is_open()) {
            if(istream.eof()) {
                Serial.printf("Stream returned EOF!");
            } else {
                int c = 0;
                int address = 0;
                char b[17] = {0};
                while(!istream.eof()) 
                {
                    char chr = istream.get();

                    if ( !istream.eof() )
                    {
                        if ( c == 0 )
                        {
                            Serial.printf("%04X: ", address);
                            address += 0x10;
                        }

                        Serial.printf("%02X ", chr);

                        // replace non-printable characters
                        if ( chr < 32 || chr > 126 )
                            chr = '.';

                        b[c] = chr;
                    }

                    // add padding
                    if ( istream.eof() && c )
                    {
                        if ( c <= 0x07 )
                        {
                            while ( c++ < 0x08 )
                                Serial.printf("   ");

                            Serial.printf("| ");
                            c--;
                        }

                        while ( c++ < 0x10 )
                            Serial.printf("   ");
                    }
                    else if ( c++ == 0x07 )
                    {
                        // add separator
                        Serial.printf("| ");
                    }

                    // show line data as ascii
                    if ( c >= 0x10 )
                    {
                        Serial.printf(" |%-16s|\r\n", b);
                        c = 0;
                        memset(b, 0, sizeof(b));
                    }
                }
                Serial.printf("\r\n");
                Serial.printf("url[%s] size[%ld]\r\n", path->url.c_str(), path->size);
            }
            istream.close();
        }
        else {
            Serial.printf("ERROR:%s could not be read!\r\n", path->url.c_str());
        }
    }

    return EXIT_SUCCESS;
}

int pwd(int argc, char **argv)
{
    Serial.printf("%s\r\n", getCurrentPath()->url.c_str());
    return EXIT_SUCCESS;
}

int cd(int argc, char **argv)
{
    const char *path;

    if (argc != 2)
    {
        path = getenv("HOME");
        if (!path)
        {
            Serial.printf("No HOME env variable set!\r\n");
            return EXIT_FAILURE;
        }
    }
    else
    {
        path = argv[1];
    }

    std::unique_ptr<MFile> destPath(getCurrentPath()->cd(argv[1]));

    Debug_printv("url[%s] path[%s] pathInStream[%s]", destPath->url.c_str(), path, destPath->pathInStream.c_str());
    if(destPath->isDirectory()) {        
        currentPath = destPath.release();
    } else {
        Serial.printf("cd: not a directory: %s\r\n", path);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int ls(int argc, char **argv)
{
    MFile* listPath = nullptr;
    const char *path_arg = nullptr;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-')
            path_arg = argv[i];
        // flags like -la, -l, -a are silently ignored
    }

    if (path_arg == nullptr)
    {
        listPath = MFSOwner::File(getCurrentPath()->url);
    }
    else
    {
        listPath = getCurrentPath()->cd(path_arg);
    }

    Debug_printv("ls path[%s]", listPath->url.c_str());
    std::unique_ptr<MFile> destPath(listPath);
    std::unique_ptr<MFile> entry(destPath->getNextFileInDir());

    if(entry.get() == nullptr) {
        // Empty directory
        return EXIT_SUCCESS;
    }

    // If sd card is mounted and we are at root
    if( getCurrentPath()->url.size() == 1 )
    {
        if ( fnSDFAT.running() )
            Serial.printf("d %8lu  'sd'\r\n", 0);

        Serial.printf("d %8lu  'network'\r\n", 0);
    }

    while(entry.get() != nullptr) {
        if ( entry->isPETSCII )
            entry->name = mstr::toUTF8(entry->name);

        Serial.printf("%c %8lu  '%s'\r\n", (entry->isDirectory()) ? 'd':'-', entry->size, entry->name.c_str());
        entry.reset(destPath->getNextFileInDir());
    }

    return EXIT_SUCCESS;
}

int mv(int argc, char **argv)
{
    if (argc != 3)
    {
        Serial.printf("Syntax is mv [ORIGIN] [TARGET]\r\n");
        return 1;
    }

    char old_name[PATH_MAX], new_name[PATH_MAX];

    // Resolve arguments to full path
    ESP32Console::console_realpath(argv[1], old_name);
    ESP32Console::console_realpath(argv[2], new_name);

    // Do rename
    if (rename(old_name, new_name))
    {
        Serial.printf("Error moving: %s\r\n", strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int cp(int argc, char **argv)
{
    //TODO: Shows weird error message
    if (argc != 3)
    {
        Serial.printf("Syntax is cp [ORIGIN] [TARGET]\r\n");
        return 1;
    }

    char old_name[PATH_MAX], new_name[PATH_MAX];

    // Resolve arguments to full path
    ESP32Console::console_realpath(argv[1], old_name);
    ESP32Console::console_realpath(argv[2], new_name);

    // Do copy
    FILE *origin = fopen(old_name, "r");
    if (!origin)
    {
        Serial.printf("Error opening origin file: %s\r\n", strerror(errno));
        return 1;
    }

    FILE *target = fopen(new_name, "w");
    if (!target)
    {
        fclose(origin);
        Serial.printf("Error opening target file: %s\r\n", strerror(errno));
        return 1;
    }

    int buffer;

    // Clear existing errors
    auto error = errno;

    while ((buffer = getc(origin)) != EOF)
    {
        if(fputc(buffer, target) == EOF) {
            Serial.printf("Error writing: %s\r\n", strerror(errno));
            fclose(origin); fclose(target);
            return 1;
        }
    }

    error = errno;
    if (error && !feof(origin))
    {
        Serial.printf("Error copying: %s\r\n", strerror(error));
        fclose(origin);
        fclose(target);
        return 1;
    }

    fclose(origin);
    fclose(target);

    return EXIT_SUCCESS;
}

int rm(int argc, char **argv)
{
    if (argc != 2)
    {
        Serial.printf("You have to pass exactly one file. Syntax rm [FILE]\r\n");
        return EXIT_SUCCESS;
    }

    char filename[PATH_MAX];
    ESP32Console::console_realpath(argv[1], filename);
    Debug_printv("argv[1][%s] filename[%s]", argv[1], filename);

    if ( strlen(filename) > 1 && filename[strlen(filename) - 1] == '*' )
    {
        char path[PATH_MAX];
        ESP32Console::console_realpath(".", path);

        DIR *dir = opendir(path);
        struct dirent *d;
        while ((d = readdir(dir)) != NULL)
        {
            std::string pattern = filename;
            std::string match_file;
            match_file.reserve(strlen(path) + 1 + strlen(d->d_name));
            match_file = path;
            if (strlen(path) > 1)
                match_file += '/';
            match_file += d->d_name;
            Debug_printv("pattern[%s] match_file[%s]", pattern.c_str(), match_file.c_str());
            if ( mstr::compare(match_file, pattern, false) )
            {
                if (remove(match_file.c_str()))
                {
                    Serial.printf("Error removing %s: %s\r\n", filename, strerror(errno));
                    closedir(dir);
                    return EXIT_FAILURE;
                }
                Serial.printf("%s removed\r\n", d->d_name);
            }
        }
        closedir(dir);
    }
    else
    {
        if(remove(filename)) {
            Serial.printf("Error removing %s: %s\r\n", filename, strerror(errno));
            return EXIT_FAILURE;
        }
        Serial.printf("%s removed\r\n", filename);
    }

    return EXIT_SUCCESS;
}

int rmdir(int argc, char **argv)
{
    if (argc != 2)
    {
        Serial.printf("You have to pass exactly one file. Syntax rmdir [DIRECTORY]\r\n");
        return EXIT_SUCCESS;
    }

    std::unique_ptr<MFile> rd(getCurrentPath()->cd(argv[1]));

    if(!rd->rmDir()) {
        Serial.printf("Error deleting %s: %s\r\n", rd->url.c_str(), strerror(errno));
    }

    return EXIT_SUCCESS;
}

int mkdir(int argc, char **argv)
{
    if (argc != 2)
    {
        Serial.printf("You have to pass exactly one file. Syntax mkdir [DIRECTORY]\r\n");
        return EXIT_SUCCESS;
    }

    std::unique_ptr<MFile> md(getCurrentPath()->cd(argv[1]));

    if(!md->mkDir()) {
        Serial.printf("Error creating %s: %s\r\n", md->url.c_str(), strerror(errno));
    }

    return EXIT_SUCCESS;
}

int mount(int argc, char **argv)
{
    if (argc < 2)
    {
        //Serial.printf("mount {device id} {url/path/filename}\r\n");

        for (int i = 0; i < MAX_DISK_DEVICES; i++)
        {
            // Show device status
            auto drive = Meatloaf.get_disks(i);
            if (drive != nullptr)
            {
                Serial.printf("#%02d: %s %s\r\n", i + 8, drive->disk_dev.getCWD().c_str(), (Config.get_device_slot_enable(i+1) ? "":"[disabled]")); //"%d: %s\r\n", drive->disk_dev.getCWD().c_str();
            }
        }

        return EXIT_SUCCESS;
    }

    if (!mstr::isNumeric(argv[1]))
    {
        Serial.printf("device id is not numeric\r\n");
        return EXIT_SUCCESS;
    }

    // Device ID
    int did = atoi(argv[1]) - 8;

    std::string filename;
    filename.reserve(getCurrentPath()->url.size() + 1);
    filename = '^';
    filename += getCurrentPath()->url;
    if ( argc > 2 )
    {
        // Use current path + filename
        if ( mstr::contains(argv[2], ":") )
        {
            filename = argv[2];
        }
        else
        {
            filename += '/';
            filename += argv[2];
        }
    }

    Debug_printv("device id[%d] url[%s]", did, filename.c_str());

    auto drive = Meatloaf.get_disks(did);
    if (drive != nullptr)
    {
        drive->disk_dev.mount(NULL, filename.c_str(), 0);
    }
    else
    {
        Serial.printf("Error mounting: device #%02d not enabled\r\n", did);
    }

    return EXIT_SUCCESS;
}

int auth(int argc, char **argv)
{
    if (argc != 3)
    {
        Serial.printf("auth {username} {password}\r\n");
        return EXIT_SUCCESS;
    }

    MFile* p = getCurrentPath();
    p->user = argv[1];
    p->password = argv[2];
    p->rebuildUrl();
    Serial.printf("Auth set for %s\r\n", p->url.c_str());

    return EXIT_SUCCESS;
}

int wget(int argc, char **argv)
{
    bool insecure = false;
    const char *url_arg = nullptr;

    if (argc == 2) {
        url_arg = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "-k") == 0) {
        insecure = true;
        url_arg = argv[2];
    } else {
        Serial.printf("wget [-k] {url}\r\n");
        return EXIT_SUCCESS;
    }

    std::string pwd = getCurrentPath()->url;

    if (insecure)
        http_set_insecure(true);

    std::unique_ptr<MFile>f(MFSOwner::File(url_arg));
    if (f != nullptr)
    {
        auto s = f->getSourceStream();

        std::string outfile;
        outfile.reserve(pwd.size() + 1 + f->name.size());
        outfile = pwd;
        outfile += '/';
        outfile += f->name;

        Debug_printv("size[%lu] name[%s] url[%s] outfile[%s]", f->size, f->name.c_str(), s->url.c_str(), outfile.c_str());


        FILE *file = fopen(outfile.c_str(), "w");
        if (file == nullptr)
        {
            Serial.printf("2 Error: Can't open file!\r\n");
            return 2;
        }

        // Receive File
        int count = 0;
        size_t total_written = 0;
        uint8_t *bytes = (uint8_t *)psram_malloc(256);
        while (true)
        {
            int bytes_read = s->read(bytes, 256);
            if (bytes_read < 1)
            {
                if (s->available())
                    Serial.printf("\nError reading '%s'\r", f->name.c_str());
                break;
            }

            int bytes_written = fwrite(bytes, 1, bytes_read, file);
            if (bytes_written != bytes_read)
            {
                Serial.printf("\nError writing '%s'\r", f->name.c_str());
                break;
            }
            total_written += bytes_written;

            // Show percentage complete in stdout
            uint8_t percent = (f->size > 0) ? (s->position() * 100) / f->size : 0;
#ifdef ENABLE_DISPLAY
            LEDS.progress = percent;
#endif
            Serial.printf("Downloading '%s' %d%% [%lu]\r", f->name.c_str(), percent, s->position());
            count++;
        }
        free(bytes);
        fclose(file);

        if (total_written == 0)
        {
            Serial.printf("\nError: Download failed, removing empty file '%s'\r\n", outfile.c_str());
            remove(outfile.c_str());
        }
        else
        {
            Serial.printf("\n");
        }
        //delete f;
    }

    if (insecure)
        http_set_insecure(false);

#ifdef ENABLE_DISPLAY
    LEDS.idle();
#endif

    return EXIT_SUCCESS;
}

int update(int argc, char **argv)
{
#if(NO_UPDATES)
    Serial.printf("Not updating; live updates disabled.\r\n");
#else
    Serial.printf("Stopping flash filesystem...\r\n");
    fsFlash.stop();

    // TODO:  Add support for SDMMC.
    Serial.println("Flash bin files from '/sd/.bin/'");
    mlff_update(PIN_SD_HOST_CS, PIN_SD_HOST_MISO, PIN_SD_HOST_MOSI, PIN_SD_HOST_SCK);

    Serial.println("Reboot to run update app and flash 'main.*.bin'...");
    esp_restart();
#endif

    return EXIT_SUCCESS;
}

int enable(int argc, char **argv)
{
    if (argc != 2)
    {
        Serial.printf("enable {id_1}|{id_1},{id_2},...\r\n");
        return EXIT_SUCCESS;
    }

    Meatloaf.enable(argv[1]);

    return EXIT_SUCCESS;
}

int disable(int argc, char **argv)
{
    if (argc != 2)
    {
        Serial.printf("disable {id_1}|{id_1},{id_2},...\r\n");
        return EXIT_SUCCESS;
    }

    Meatloaf.disable(argv[1]);

    return EXIT_SUCCESS;
}

static void df_print_row(const char *label, const char *path, uint64_t total, uint64_t avail)
{
    uint64_t used = total - avail;
    uint32_t pct  = total ? (uint32_t)(used * 100 / total) : 0;
    Serial.printf("%-6s  %8lu KB  %8lu KB  %8lu KB  %3lu%%  %s\r\n",
        label,
        (unsigned long)(total / 1024),
        (unsigned long)(used  / 1024),
        (unsigned long)(avail / 1024),
        (unsigned long)pct,
        path);
}

static int df(int argc, char **argv)
{
    Serial.printf("%-6s  %11s  %11s  %11s  %4s  %s\r\n",
        "FS", "Size", "Used", "Avail", "Use%", "Mounted on");

    size_t lfs_total = 0, lfs_used = 0;
    if (esp_littlefs_info("storage", &lfs_total, &lfs_used) == ESP_OK)
        df_print_row("flash", "/", lfs_total, lfs_total - lfs_used);
    else
        Serial.printf("%-6s  not available\r\n", "flash");

#ifdef SD_CARD
    uint64_t fat_total = 0, fat_free = 0;
    if (esp_vfs_fat_info("/sd", &fat_total, &fat_free) == ESP_OK)
        df_print_row("sd", "/sd", fat_total, fat_free);
    else
        Serial.printf("%-6s  not available\r\n", "sd");
#endif

    return EXIT_SUCCESS;
}

#ifdef SD_CARD
// ─── locate / updatedb ────────────────────────────────────────────────────────
#include "sqlite3.h"
#include "sqlite3_esp32.h"
#include <string>
#include <unordered_set>
#include <vector>
#include <ctime>

#include "string_utils.h"

#define LOCATE_DB_PATH "/sd/.locate"

/* Volatile scan state — written by the scan task, read by locate/updatedb. */
static volatile int    s_scan_running = 0;
static volatile int    s_scan_stop    = 0;  // set to 1 to request cancellation
static volatile int    s_scan_resume  = 0;  // set to 1 to resume from existing DB
static volatile int    s_scan_files   = 0;
static volatile int    s_scan_dirs    = 0;
static volatile int    s_scan_errors  = 0;
static volatile time_t s_scan_start   = 0;
static volatile time_t s_scan_end     = 0;
static std::string     s_scan_last_folder;  // last completed directory path

static void sqlite_one_time_init(void)
{
    static bool inited = false;
    if (inited) return;
    int psram = sqlite3_esp32_init();
    Serial.printf("sqlite: pcache=%s\r\n", psram ? "PSRAM" : "DRAM(fallback)");
    inited = true;
}

/* Directory path entry allocated from PSRAM.
 * std::string's internal heap allocation for paths > ~15 chars lands in
 * internal DRAM, which quickly exhausts the DMA-capable heap and breaks
 * SDMMC reads at scale (57K+ files / 4K+ dirs).  Storing paths in PSRAM
 * keeps internal DRAM free for the SDMMC DMA allocator. */
struct PsramPath {
    char *s = nullptr;
    explicit PsramPath(const char *str) {
        size_t n = strlen(str) + 1;
        s = (char *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s) s = (char *)malloc(n);
        if (s) memcpy(s, str, n);
    }
    ~PsramPath() { free(s); }
    PsramPath(PsramPath &&o) noexcept : s(o.s) { o.s = nullptr; }
    PsramPath(const PsramPath &) = delete;
    PsramPath &operator=(const PsramPath &) = delete;
};

// dir_ins_stmt : INSERT OR IGNORE INTO dirs (path) VALUES (?)
// dir_id_stmt  : SELECT id FROM dirs WHERE path = ?
// file_ins_stmt: INSERT OR REPLACE INTO files (dir_id, name, size, mtime, is_dir) VALUES (?,?,?,?,?)
// mark_stmt    : UPDATE dirs SET scanned=1 WHERE path=?
static void updatedb_scan(sqlite3 *db,
                           sqlite3_stmt *dir_ins_stmt,
                           sqlite3_stmt *dir_id_stmt,
                           sqlite3_stmt *file_ins_stmt,
                           sqlite3_stmt *mark_stmt,
                           std::vector<PsramPath> dirs,
                           const std::unordered_set<std::string>& skip_dirs)
{
    char full[PATH_MAX];
    int  batch = 0;

    auto mark_scanned = [&](const char *rel) {
        sqlite3_reset(mark_stmt);
        sqlite3_bind_text(mark_stmt, 1, rel, -1, SQLITE_STATIC);
        sqlite3_step(mark_stmt);
    };

    // Ensure a directory exists in `dirs` and return its id.
    auto get_or_create_dir = [&](const char *rel) -> sqlite3_int64 {
        sqlite3_reset(dir_ins_stmt);
        sqlite3_bind_text(dir_ins_stmt, 1, rel, -1, SQLITE_STATIC);
        sqlite3_step(dir_ins_stmt);
        sqlite3_reset(dir_id_stmt);
        sqlite3_bind_text(dir_id_stmt, 1, rel, -1, SQLITE_STATIC);
        sqlite3_int64 id = 0;
        if (sqlite3_step(dir_id_stmt) == SQLITE_ROW)
            id = sqlite3_column_int64(dir_id_stmt, 0);
        return id;
    };

    while (!dirs.empty() && !s_scan_stop) {
        PsramPath cur(std::move(dirs.back()));
        dirs.pop_back();
        if (!cur.s) continue;

        const char *cur_rel = cur.s + 3;   // relative to /sd, e.g. "" for root, "/games" for subdir
        sqlite3_int64 cur_dir_id = get_or_create_dir(cur_rel);

        DIR *d = opendir(cur.s);
        if (!d) {
            mark_scanned(cur_rel);
            continue;
        }

        struct dirent *ent;
        while (!s_scan_stop && (ent = readdir(d)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            if (strcmp(cur.s, "/sd") == 0 && strcmp(ent->d_name, ".locate") == 0)
                continue;

            snprintf(full, sizeof(full), "%s/%s", cur.s, ent->d_name);

            if (mstr::isJunk(ent->d_name)) {
                if (remove(full) == 0)
                    Serial.printf("  deleted: %s\r\n", full);
                continue;
            }

            struct stat st;
            if (stat(full, &st) != 0) continue;

            int is_dir = S_ISDIR(st.st_mode) ? 1 : 0;

            sqlite3_reset(file_ins_stmt);
            sqlite3_bind_int64(file_ins_stmt, 1, cur_dir_id);
            sqlite3_bind_text(file_ins_stmt, 2, ent->d_name, -1, SQLITE_STATIC);
            sqlite3_bind_int64(file_ins_stmt, 3, (sqlite3_int64)st.st_size);
            sqlite3_bind_int64(file_ins_stmt, 4, (sqlite3_int64)st.st_mtime);
            sqlite3_bind_int(file_ins_stmt, 5, is_dir);
            int rc = sqlite3_step(file_ins_stmt);
            if (rc != SQLITE_DONE) {
                s_scan_errors = s_scan_errors + 1;
                if (s_scan_errors <= 3)
                    Serial.printf("  insert error %d: %s — %s/%s\r\n",
                                  rc, sqlite3_errmsg(db), cur_rel, ent->d_name);
            }

            if (is_dir) {
                s_scan_dirs = s_scan_dirs + 1;
                const char *sub_rel = full + 3;
                get_or_create_dir(sub_rel);  // ensure dirs row exists for resume tracking
                if (skip_dirs.count(sub_rel))
                    mark_scanned(sub_rel);
                else
                    dirs.emplace_back(full);
            } else {
                s_scan_files = s_scan_files + 1;
            }

            if (++batch >= 1000) {
                if (!sqlite3_get_autocommit(db)) {
                    char *cerr = nullptr;
                    if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, &cerr) != SQLITE_OK) {
                        Serial.printf("  commit failed: %s\r\n",
                                      cerr ? cerr : sqlite3_errmsg(db));
                        sqlite3_free(cerr);
                    }
                }
                char *berr = nullptr;
                if (sqlite3_exec(db, "BEGIN", nullptr, nullptr, &berr) != SQLITE_OK) {
                    Serial.printf("  BEGIN failed: %s\r\n",
                                  berr ? berr : sqlite3_errmsg(db));
                    sqlite3_free(berr);
                }
                batch = 0;
            }

            int total = s_scan_files + s_scan_dirs;
            if (total % 100 == 0)
                Serial.printf("  %d dirs, %d files — free=%lu dma_max=%lu\r\n",
                              (int)s_scan_dirs, (int)s_scan_files,
                              esp_get_free_internal_heap_size(),
                              (unsigned long)heap_caps_get_largest_free_block(
                                  MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        }
        closedir(d);
        mark_scanned(cur_rel);
        s_scan_last_folder = cur_rel;
        vTaskDelay(1);
    }
}

// DELETE journal (the SQLite default) writes the rollback journal to SD and
// deletes it on each COMMIT.  This properly resets the pager's internal
// state on every COMMIT/BEGIN cycle, fixing the "cannot commit - no
// transaction is active" error that journal_mode=OFF has after 2+ cycles
// with SQLITE_DEFAULT_LOCKING_MODE=1 (EXCLUSIVE).
// MEMORY journal was tried but rejected: it allocates ~520-byte entries in
// internal DRAM (below SPIRAM_MALLOC_ALWAYSINTERNAL threshold), exhausting
// the DMA-capable heap and breaking SDMMC writes at row ~300.
static void apply_pragmas(sqlite3 *d)
{
    sqlite3_exec(d, "PRAGMA journal_mode = DELETE", nullptr, nullptr, nullptr);
    sqlite3_exec(d, "PRAGMA synchronous = OFF",     nullptr, nullptr, nullptr);
    // 128 pages from the pre-allocated PSRAM slab (see sqlite3_esp32_init).
    sqlite3_exec(d, "PRAGMA cache_size = 128",      nullptr, nullptr, nullptr);
}

static void updatedb_compress_gz(void);

// Rebuild the FTS5 index from the existing files+dirs tables.
// Swaps SQLite to the PSRAM allocator for the duration so FTS5's token hash
// doesn't exhaust internal DRAM (see sqlite3_esp32_psram_malloc_enter).
// Prints progress every 100 rows.  Safe to call from any FreeRTOS task.
static void updatedb_fts_rebuild(void)
{
    sqlite_one_time_init();
    sqlite3_esp32_psram_malloc_enter();

    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(LOCATE_DB_PATH, &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        Serial.printf("updatedb fts: open failed: %s\r\n", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        sqlite3_esp32_psram_malloc_exit();
        return;
    }
    apply_pragmas(db);

    int fts_total = 0;
    {
        sqlite3_stmt *cnt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT count(*) FROM files", -1, &cnt, nullptr) == SQLITE_OK
                && sqlite3_step(cnt) == SQLITE_ROW)
            fts_total = sqlite3_column_int(cnt, 0);
        sqlite3_finalize(cnt);
    }

    if (fts_total == 0) {
        Serial.printf("updatedb fts: no files in database — run 'updatedb start' first\r\n");
        sqlite3_close(db);
        sqlite3_esp32_psram_malloc_exit();
        return;
    }

    Serial.printf("updatedb: clearing FTS index...\r\n");
    sqlite3_exec(db, "INSERT INTO files_fts(files_fts) VALUES('delete-all')",
                 nullptr, nullptr, nullptr);
    Serial.printf("updatedb: building FTS index (%d rows)...\r\n", fts_total);

    sqlite3_stmt *sel_stmt = nullptr;
    sqlite3_stmt *ins_stmt = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT files.id, dirs.path || '/' || files.name"
        " FROM files JOIN dirs ON dirs.id = files.dir_id"
        " ORDER BY files.id",
        -1, &sel_stmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT INTO files_fts(rowid, path) VALUES(?, ?)",
        -1, &ins_stmt, nullptr);

    int fts_done = 0;
    time_t fts_start = time(nullptr);
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);

    while (sqlite3_step(sel_stmt) == SQLITE_ROW) {
        sqlite3_reset(ins_stmt);
        sqlite3_bind_int64(ins_stmt, 1, sqlite3_column_int64(sel_stmt, 0));
        sqlite3_bind_text(ins_stmt, 2,
            (const char *)sqlite3_column_text(sel_stmt, 1), -1, SQLITE_STATIC);
        sqlite3_step(ins_stmt);

        if (++fts_done % 100 == 0) {
            int pct = fts_total > 0 ? fts_done * 100 / fts_total : 0;
            Serial.printf("  %d / %d  (%d%%)  %s\r\n",
                          fts_done, fts_total, pct,
                          mstr::formatDuration((long)(time(nullptr) - fts_start)).c_str());
            sqlite3_exec(db, "COMMIT;BEGIN", nullptr, nullptr, nullptr);
            vTaskDelay(1);
        }
    }
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_finalize(sel_stmt);
    sqlite3_finalize(ins_stmt);

    Serial.printf("  %d rows indexed in %s.\r\n",
                  fts_done, mstr::formatDuration((long)(time(nullptr) - fts_start)).c_str());

    sqlite3_close(db);
    sqlite3_esp32_psram_malloc_exit();
    updatedb_compress_gz();
}

static void updatedb_compress_gz(void)
{
    struct stat st = {};
    size_t total = (stat(LOCATE_DB_PATH, &st) == 0) ? (size_t)st.st_size : 0;
    Serial.printf("updatedb: compressing to %s.gz (%zu bytes)...\r\n", LOCATE_DB_PATH, total);

    FILE *in = fopen(LOCATE_DB_PATH, "rb");
    if (!in) {
        Serial.printf("updatedb compress: cannot open %s\r\n", LOCATE_DB_PATH);
        return;
    }

    gzFile gz = gzopen(LOCATE_DB_PATH ".gz", "wb9");
    if (!gz) {
        Serial.printf("updatedb compress: cannot create %s.gz\r\n", LOCATE_DB_PATH);
        fclose(in);
        return;
    }

    const size_t kBufSz = 32768;
    char *buf = (char *)psram_malloc(kBufSz);
    if (!buf) {
        Serial.printf("updatedb compress: out of memory\r\n");
        gzclose(gz);
        fclose(in);
        return;
    }

    size_t written = 0, last_report = 0;
    const size_t kReport = 512 * 1024;
    size_t n;
    while ((n = fread(buf, 1, kBufSz, in)) > 0) {
        gzwrite(gz, buf, (unsigned)n);
        written += n;
        if (total > 0 && written - last_report >= kReport) {
            Serial.printf("  %zu / %zu bytes (%d%%)\r\n",
                          written, total, (int)(written * 100 / total));
            last_report = written;
            vTaskDelay(1);
        }
    }

    free(buf);
    gzclose(gz);
    fclose(in);
    if (total > 0 && last_report < written)
        Serial.printf("  %zu / %zu bytes (100%%)\r\n", written, total);
    Serial.printf("updatedb: %s.gz written\r\n", LOCATE_DB_PATH);
}

static void updatedb_fts_task(void *arg)
{
    updatedb_fts_rebuild();
    s_scan_running = 0;
    vTaskDelete(NULL);
}

static void updatedb_task(void *arg)
{
    // SQLITE_OMIT_AUTOINIT: must init before sqlite3_open().
    sqlite_one_time_init();

    sqlite3      *db            = nullptr;
    sqlite3_stmt *dir_ins_stmt  = nullptr;   // INSERT OR IGNORE INTO dirs (path)
    sqlite3_stmt *dir_id_stmt   = nullptr;   // SELECT id FROM dirs WHERE path=?
    sqlite3_stmt *file_ins_stmt = nullptr;   // INSERT OR REPLACE INTO files (dir_id,name,...)
    sqlite3_stmt *mark_stmt     = nullptr;   // UPDATE dirs SET scanned=1 WHERE path=?
    std::vector<PsramPath>          initial_dirs;
    std::unordered_set<std::string> skip_dirs;

    if (!s_scan_resume) {
        // ── Fresh scan ────────────────────────────────────────────────────────
        // Remove any stale/corrupt database from a previous interrupted scan.
        unlink(LOCATE_DB_PATH);
        unlink(LOCATE_DB_PATH ".gz");

        if (sqlite3_open(LOCATE_DB_PATH, &db) != SQLITE_OK) {
            Serial.printf("updatedb: cannot create database: %s\r\n",
                          db ? sqlite3_errmsg(db) : "out of memory");
            if (db) sqlite3_close(db);
            s_scan_running = 0;
            vTaskDelete(NULL);
            return;
        }

        Serial.printf("updatedb: db open — free=%lu dma_max=%lu\r\n",
                      esp_get_free_internal_heap_size(),
                      (unsigned long)heap_caps_get_largest_free_block(
                          MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));

        apply_pragmas(db);

        const char *schema =
            "DROP TABLE IF EXISTS files_fts;"
            "DROP TABLE IF EXISTS files;"
            "DROP TABLE IF EXISTS dirs;"
            // dirs: one row per unique directory path; scanned=1 once all children are indexed.
            "CREATE TABLE dirs ("
            "  id      INTEGER PRIMARY KEY,"
            "  path    TEXT    NOT NULL UNIQUE,"
            "  scanned INTEGER NOT NULL DEFAULT 0"
            ");"
            // files: every entry (file or subdir) stored under its parent dir_id.
            // UNIQUE(dir_id,name) lets INSERT OR REPLACE handle re-scans cleanly.
            "CREATE TABLE files ("
            "  id      INTEGER PRIMARY KEY,"
            "  dir_id  INTEGER NOT NULL,"
            "  name    TEXT    NOT NULL,"
            "  size    INTEGER NOT NULL DEFAULT 0,"
            "  mtime   INTEGER NOT NULL DEFAULT 0,"
            "  is_dir  INTEGER NOT NULL DEFAULT 0,"
            "  UNIQUE(dir_id, name)"
            ");"
            "CREATE INDEX files_dir_idx ON files(dir_id);"
            // FTS5 with content='' stores only the inverted token index, not the
            // original text — the full path is reconstructed via JOIN at query time.
            "CREATE VIRTUAL TABLE files_fts USING fts5("
            "  path, content='', tokenize=\"unicode61\""
            ");"
            "CREATE TABLE status ("
            "  id          INTEGER PRIMARY KEY DEFAULT 1,"
            "  total_dirs  INTEGER NOT NULL DEFAULT 0,"
            "  total_files INTEGER NOT NULL DEFAULT 0,"
            "  last_scan   INTEGER NOT NULL DEFAULT 0,"
            "  duration    INTEGER NOT NULL DEFAULT 0,"
            "  last_folder TEXT    NOT NULL DEFAULT ''"
            ");";

        char *errmsg = nullptr;
        if (sqlite3_exec(db, schema, nullptr, nullptr, &errmsg) != SQLITE_OK) {
            Serial.printf("updatedb: %s\r\n", errmsg);
            sqlite3_free(errmsg);
            sqlite3_close(db);
            s_scan_running = 0;
            vTaskDelete(NULL);
            return;
        }

        initial_dirs.emplace_back("/sd");

    } else {
        // ── Resume scan ───────────────────────────────────────────────────────
        if (sqlite3_open(LOCATE_DB_PATH, &db) != SQLITE_OK) {
            Serial.printf("updatedb: cannot open database for resume: %s\r\n",
                          db ? sqlite3_errmsg(db) : "out of memory");
            if (db) sqlite3_close(db);
            s_scan_running = 0;
            vTaskDelete(NULL);
            return;
        }

        apply_pragmas(db);

        // Detect old single-table schema (no `dirs` table).
        {
            sqlite3_stmt *chk = nullptr;
            bool has_dirs = (sqlite3_prepare_v2(db, "SELECT id FROM dirs LIMIT 1",
                                                -1, &chk, nullptr) == SQLITE_OK);
            sqlite3_finalize(chk);
            if (!has_dirs) {
                Serial.printf("updatedb: schema outdated — run 'updatedb start' to rebuild.\r\n");
                sqlite3_close(db);
                s_scan_running = 0;
                vTaskDelete(NULL);
                return;
            }
        }

        // Migrate: add status table if absent.
        sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS status ("
            "  id INTEGER PRIMARY KEY DEFAULT 1,"
            "  total_files INTEGER NOT NULL DEFAULT 0,"
            "  total_dirs  INTEGER NOT NULL DEFAULT 0,"
            "  last_scan   INTEGER NOT NULL DEFAULT 0,"
            "  duration    INTEGER NOT NULL DEFAULT 0,"
            "  last_folder TEXT    NOT NULL DEFAULT ''"
            ")",
            nullptr, nullptr, nullptr);

        // Load fully-scanned directories so we don't re-process them.
        {
            sqlite3_stmt *q = nullptr;
            if (sqlite3_prepare_v2(db,
                    "SELECT path FROM dirs WHERE scanned=1",
                    -1, &q, nullptr) == SQLITE_OK) {
                while (sqlite3_step(q) == SQLITE_ROW) {
                    const char *p = (const char *)sqlite3_column_text(q, 0);
                    if (p) skip_dirs.insert(p);
                }
                sqlite3_finalize(q);
            }
        }

        // Always re-scan /sd root so any root-level entries missing from the DB are picked up.
        initial_dirs.emplace_back("/sd");

        // Seed with directories that were discovered but not yet fully scanned.
        {
            sqlite3_stmt *q = nullptr;
            if (sqlite3_prepare_v2(db,
                    "SELECT path FROM dirs WHERE scanned=0",
                    -1, &q, nullptr) == SQLITE_OK) {
                while (sqlite3_step(q) == SQLITE_ROW) {
                    const char *p = (const char *)sqlite3_column_text(q, 0);
                    if (p && p[0] != '\0') {
                        std::string full = "/sd";
                        full += p;
                        initial_dirs.emplace_back(full.c_str());
                    }
                }
                sqlite3_finalize(q);
            }
        }

        Serial.printf("updatedb: resuming — %zu pending dirs, %zu already scanned\r\n",
                      initial_dirs.size(), skip_dirs.size());
    }

    // Prepare the four statements used by updatedb_scan.
    auto prep_fail = [&](const char *label) {
        Serial.printf("updatedb: prepare %s failed: %s\r\n", label, sqlite3_errmsg(db));
        sqlite3_finalize(dir_ins_stmt);
        sqlite3_finalize(dir_id_stmt);
        sqlite3_finalize(file_ins_stmt);
        sqlite3_finalize(mark_stmt);
        sqlite3_close(db);
        s_scan_running = 0;
        vTaskDelete(NULL);
    };

    if (sqlite3_prepare_v2(db,
            "INSERT OR IGNORE INTO dirs (path) VALUES (?)",
            -1, &dir_ins_stmt, nullptr) != SQLITE_OK) { prep_fail("dir_ins"); return; }

    if (sqlite3_prepare_v2(db,
            "SELECT id FROM dirs WHERE path=?",
            -1, &dir_id_stmt, nullptr) != SQLITE_OK) { prep_fail("dir_id"); return; }

    if (sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO files (dir_id, name, size, mtime, is_dir)"
            " VALUES (?, ?, ?, ?, ?)",
            -1, &file_ins_stmt, nullptr) != SQLITE_OK) { prep_fail("file_ins"); return; }

    if (sqlite3_prepare_v2(db,
            "UPDATE dirs SET scanned=1 WHERE path=?",
            -1, &mark_stmt, nullptr) != SQLITE_OK) { prep_fail("mark"); return; }

    s_scan_errors = 0;
    char *begin_err = nullptr;
    if (sqlite3_exec(db, "BEGIN", nullptr, nullptr, &begin_err) != SQLITE_OK) {
        Serial.printf("updatedb: initial BEGIN failed: %s\r\n",
                      begin_err ? begin_err : sqlite3_errmsg(db));
        sqlite3_free(begin_err);
    }
    updatedb_scan(db, dir_ins_stmt, dir_id_stmt, file_ins_stmt, mark_stmt,
                  std::move(initial_dirs), skip_dirs);

    // Commit whatever the last partial batch left in-transaction.
    if (!sqlite3_get_autocommit(db)) {
        char *commit_err = nullptr;
        if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, &commit_err) != SQLITE_OK) {
            Serial.printf("updatedb: final COMMIT failed: %s\r\n",
                          commit_err ? commit_err : sqlite3_errmsg(db));
            sqlite3_free(commit_err);
        }
    }

    sqlite3_finalize(dir_ins_stmt);
    sqlite3_finalize(dir_id_stmt);
    sqlite3_finalize(file_ins_stmt);
    sqlite3_finalize(mark_stmt);
    sqlite3_close(db);   /* must close before fts_rebuild calls sqlite3_shutdown() */
    db = nullptr;

    if (!s_scan_stop)
        updatedb_fts_rebuild();

    s_scan_end = time(nullptr);

    // Persist scan statistics so 'updatedb' (no args) can display them later.
    {
        sqlite3 *sdb = nullptr;
        if (sqlite3_open_v2(LOCATE_DB_PATH, &sdb, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK) {
            apply_pragmas(sdb);
            sqlite3_stmt *ss = nullptr;
            const char *status_sql =
                "INSERT OR REPLACE INTO status"
                " (id, total_dirs, total_files, last_scan, duration, last_folder)"
                " VALUES (1, ?, ?, ?, ?, ?)";
            if (sqlite3_prepare_v2(sdb, status_sql, -1, &ss, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(ss,   1, (int)s_scan_dirs);
                sqlite3_bind_int(ss,   2, (int)s_scan_files);
                sqlite3_bind_int64(ss, 3, (sqlite3_int64)s_scan_end);
                sqlite3_bind_int64(ss, 4, (sqlite3_int64)(s_scan_end - s_scan_start));
                sqlite3_bind_text(ss,  5, s_scan_last_folder.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(ss);
                sqlite3_finalize(ss);
            }
            sqlite3_close(sdb);
        }
    }

    s_scan_resume  = 0;
    s_scan_running = 0;

    if (s_scan_stop)
        Serial.printf("updatedb: stopped — %d directories, %d files, last folder: %s\r\n",
                      (int)s_scan_dirs, (int)s_scan_files, s_scan_last_folder.c_str());
    else
        Serial.printf("updatedb: done — %d directories, %d files, %d errors, %s.\r\n",
                      (int)s_scan_dirs, (int)s_scan_files,
                      (int)s_scan_errors, mstr::formatDuration(s_scan_end - s_scan_start).c_str());
    vTaskDelete(NULL);
}

int updatedb(int argc, char **argv)
{
    // ── updatedb (no args) → show persistent status ──────────────────────────
    if (argc < 2) {
        if (s_scan_running) {
            time_t elapsed = time(nullptr) - s_scan_start;
            Serial.printf("Scan in progress: %d directories, %d files (%s elapsed)\r\n",
                          (int)s_scan_dirs, (int)s_scan_files, mstr::formatDuration(elapsed).c_str());
            if (!s_scan_last_folder.empty())
                Serial.printf("Last folder:      /sd%s\r\n", s_scan_last_folder.c_str());
            return EXIT_SUCCESS;
        }
        if (!fnSDFAT.running()) {
            Serial.printf("updatedb: SD card not mounted\r\n");
            return EXIT_FAILURE;
        }
        sqlite_one_time_init();
        sqlite3 *db = nullptr;
        if (sqlite3_open_v2(LOCATE_DB_PATH, &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            Serial.printf("No index found. Run 'updatedb start' to build the database.\r\n");
            if (db) sqlite3_close(db);
            return EXIT_SUCCESS;
        }
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db,
                "SELECT total_dirs, total_files, last_scan, duration, last_folder"
                " FROM status WHERE id=1",
                -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
            int    total_dirs   = sqlite3_column_int(stmt, 0);
            int    total_files  = sqlite3_column_int(stmt, 1);
            time_t last_scan    = (time_t)sqlite3_column_int64(stmt, 2);
            int    duration     = sqlite3_column_int(stmt, 3);
            const char *folder  = (const char *)sqlite3_column_text(stmt, 4);
            char tbuf[32] = "unknown";
            struct tm *ti = localtime(&last_scan);
            if (ti) strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", ti);
            Serial.printf("Last scan:    %s (%s)\r\n", tbuf, mstr::formatDuration(duration).c_str());
            Serial.printf("Index:       %d directories, %d files\r\n", total_dirs, total_files);
            if (folder && folder[0])
                Serial.printf("Last folder:  /sd%s\r\n", folder);
        } else {
            Serial.printf("No scan data. Run 'updatedb start' to build the database.\r\n");
        }
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return EXIT_SUCCESS;
    }

    // ── updatedb stop ─────────────────────────────────────────────────────────
    if (strcmp(argv[1], "stop") == 0) {
        if (!s_scan_running) {
            Serial.printf("updatedb: no scan in progress\r\n");
            return EXIT_FAILURE;
        }
        s_scan_stop = 1;
        Serial.printf("updatedb: stop requested\r\n");
        return EXIT_SUCCESS;
    }

    // ── updatedb resume ───────────────────────────────────────────────────────
    if (strcmp(argv[1], "resume") == 0) {
        if (!fnSDFAT.running()) {
            Serial.printf("updatedb: SD card not mounted\r\n");
            return EXIT_FAILURE;
        }
        if (s_scan_running) {
            Serial.printf("updatedb: scan already in progress (%d directories, %d files so far)\r\n",
                          (int)s_scan_dirs, (int)s_scan_files);
            return EXIT_FAILURE;
        }
        struct stat dbst;
        if (stat(LOCATE_DB_PATH, &dbst) != 0) {
            Serial.printf("updatedb: no database to resume — run 'updatedb start' first\r\n");
            return EXIT_FAILURE;
        }
        s_scan_files        = 0;
        s_scan_dirs         = 0;
        s_scan_errors       = 0;
        s_scan_stop         = 0;
        s_scan_resume       = 1;
        s_scan_start        = time(nullptr);
        s_scan_end          = 0;
        s_scan_last_folder  = "";
        s_scan_running      = 1;
        Serial.printf("Resuming locate database scan in background...\r\n");
        xTaskCreate(updatedb_task, "updatedb", 8192, nullptr, 5, nullptr);
        return EXIT_SUCCESS;
    }

    // ── updatedb start ────────────────────────────────────────────────────────
    if (strcmp(argv[1], "start") == 0) {
        if (!fnSDFAT.running()) {
            Serial.printf("updatedb: SD card not mounted\r\n");
            return EXIT_FAILURE;
        }
        if (s_scan_running) {
            Serial.printf("updatedb: scan already in progress (%d directories, %d files so far)\r\n",
                          (int)s_scan_dirs, (int)s_scan_files);
            return EXIT_FAILURE;
        }
        s_scan_files        = 0;
        s_scan_dirs         = 0;
        s_scan_errors       = 0;
        s_scan_stop         = 0;
        s_scan_resume       = 0;
        s_scan_start        = time(nullptr);
        s_scan_end          = 0;
        s_scan_last_folder  = "";
        s_scan_running      = 1;
        Serial.printf("Building locate database in background...\r\n");
        xTaskCreate(updatedb_task, "updatedb", 8192, nullptr, 5, nullptr);
        return EXIT_SUCCESS;
    }

    // ── updatedb fts ─────────────────────────────────────────────────────────
    if (strcmp(argv[1], "fts") == 0) {
        if (!fnSDFAT.running()) {
            Serial.printf("updatedb: SD card not mounted\r\n");
            return EXIT_FAILURE;
        }
        if (s_scan_running) {
            Serial.printf("updatedb: scan already in progress — wait for it to finish\r\n");
            return EXIT_FAILURE;
        }
        struct stat dbst;
        if (stat(LOCATE_DB_PATH, &dbst) != 0) {
            Serial.printf("updatedb: no database — run 'updatedb start' first\r\n");
            return EXIT_FAILURE;
        }
        s_scan_running = 1;
        Serial.printf("Rebuilding FTS index in background...\r\n");
        xTaskCreate(updatedb_fts_task, "updatedb_fts", 8192, nullptr, 5, nullptr);
        return EXIT_SUCCESS;
    }

    Serial.printf("Usage: updatedb [start|stop|resume|fts]\r\n");
    return EXIT_FAILURE;
}

int locate(int argc, char **argv)
{
    if (argc < 2) {
        if (s_scan_running) {
            time_t elapsed = time(nullptr) - s_scan_start;
            Serial.printf("Scan in progress: %d directories, %d files indexed (%s elapsed)\r\n",
                          (int)s_scan_dirs, (int)s_scan_files, mstr::formatDuration(elapsed).c_str());
        } else if (s_scan_end > 0) {
            Serial.printf("Last scan: %d directories, %d files in %s\r\n",
                          (int)s_scan_dirs, (int)s_scan_files,
                          mstr::formatDuration(s_scan_end - s_scan_start).c_str());
        } else {
            /* No scan this session — query the database for persisted totals. */
            if (!fnSDFAT.running()) {
                Serial.printf("locate: SD card not mounted\r\n");
                return EXIT_FAILURE;
            }
            sqlite_one_time_init();
            sqlite3 *db = nullptr;
            if (sqlite3_open_v2(LOCATE_DB_PATH, &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
                Serial.printf("No index found. Run 'updatedb start' to build the database.\r\n");
                if (db) sqlite3_close(db);
                return EXIT_SUCCESS;
            }
            sqlite3_stmt *stmt = nullptr;
            int files = 0, dirs = 0;
            if (sqlite3_prepare_v2(db,
                    "SELECT SUM(is_dir=0), SUM(is_dir=1) FROM files",
                    -1, &stmt, nullptr) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
                files = sqlite3_column_int(stmt, 0);
                dirs  = sqlite3_column_int(stmt, 1);
            }
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            Serial.printf("Index contains %d directories, %d files. Run 'updatedb start' to refresh.\r\n",
                          dirs, files);
        }
        return EXIT_SUCCESS;
    }

    if (!fnSDFAT.running()) {
        Serial.printf("locate: SD card not mounted\r\n");
        return EXIT_FAILURE;
    }
    if (s_scan_running) {
        Serial.printf("locate: scan in progress, please wait.\r\n");
        return EXIT_FAILURE;
    }

    sqlite_one_time_init();  // SQLITE_OMIT_AUTOINIT: required before sqlite3_open

    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(LOCATE_DB_PATH, &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        Serial.printf("locate: database not found. Run 'updatedb' first.\r\n");
        if (db) sqlite3_close(db);
        return EXIT_FAILURE;
    }

    const char *arg = argv[1];
    size_t arg_len = strlen(arg);

    // FTS5 only handles trailing-* prefix queries.  Leading wildcards (*foo,
    // ?foo), mid-string * (f*o), or any ? are invalid FTS5 syntax — skip FTS
    // and go straight to LIKE for those patterns.
    bool leading_wild = (arg[0] == '*' || arg[0] == '?');
    bool has_question = (strchr(arg, '?') != nullptr);
    bool mid_star = false;
    for (size_t i = 0; i + 1 < arg_len; i++)
        if (arg[i] == '*') { mid_star = true; break; }
    bool use_fts = !leading_wild && !has_question && !mid_star;

    // Build FTS5 pattern: bare words get a trailing * for prefix match.
    std::string fts_pattern(arg);
    if (use_fts && !strchr(arg, '*') && !strchr(arg, '"'))
        fts_pattern += "*";

    // Build LIKE pattern: convert glob wildcards, wrap bare words in %.
    bool has_wildcards = strchr(arg, '*') || strchr(arg, '?');
    std::string like_pattern(arg);
    for (char &c : like_pattern) {
        if (c == '*') c = '%';
        else if (c == '?') c = '_';
    }
    if (!has_wildcards) like_pattern = "%" + like_pattern + "%";

    const char *fts_sql =
        "SELECT dirs.path || '/' || files.name, files.size, files.is_dir"
        " FROM files JOIN dirs ON dirs.id = files.dir_id"
        " WHERE files.id IN (SELECT rowid FROM files_fts WHERE files_fts MATCH ?)"
        " ORDER BY dirs.path, files.name";
    const char *like_sql =
        "SELECT dirs.path || '/' || files.name, files.size, files.is_dir"
        " FROM files JOIN dirs ON dirs.id = files.dir_id"
        " WHERE dirs.path || '/' || files.name LIKE ?"
        " ORDER BY dirs.path, files.name";

    int count = 0;
    sqlite3_stmt *stmt = nullptr;

    auto drain = [&](sqlite3_stmt *s) {
        while (sqlite3_step(s) == SQLITE_ROW) {
            const char *p = (const char *)sqlite3_column_text(s, 0);
            sqlite3_int64 sz = sqlite3_column_int64(s, 1);
            int is_dir = sqlite3_column_int(s, 2);
            if (p) Serial.printf("%c %8lld  /sd%s\r\n", is_dir ? 'd' : '-', (long long)sz, p);
            count++;
        }
    };

    // Try FTS5 first (only when pattern is FTS-compatible).
    if (use_fts && sqlite3_prepare_v2(db, fts_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fts_pattern.c_str(), -1, SQLITE_TRANSIENT);
        drain(stmt);
        sqlite3_finalize(stmt);
        stmt = nullptr;
    }

    // Fall back to LIKE if FTS was skipped, returned nothing, or wasn't available.
    if (count == 0) {
        if (sqlite3_prepare_v2(db, like_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, like_pattern.c_str(), -1, SQLITE_TRANSIENT);
            drain(stmt);
            sqlite3_finalize(stmt);
        } else {
            Serial.printf("locate: database schema outdated — run 'updatedb start' to rebuild.\r\n");
            sqlite3_close(db);
            return EXIT_FAILURE;
        }
    }

    sqlite3_close(db);

    if (count == 0)
        Serial.printf("locate: no matches for '%s'\r\n", argv[1]);

    return EXIT_SUCCESS;
}
// ─── end locate / updatedb ────────────────────────────────────────────────────

static void format_sd_task(void *arg)
{
    Serial.printf("Formatting SD card (this may take several minutes)...\r\n");
    if (fnSDFAT.format())
    {
        Serial.printf("SD card formatted successfully.\r\n");
    }
    else
    {
        Serial.printf("SD card format FAILED.\r\n");
    }
    vTaskDelete(NULL);
}

int format_sd(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "-y") != 0)
    {
        Serial.printf("WARNING: This will erase all data on the SD card!\r\n");
        Serial.printf("Usage: format_sd -y\r\n");
        return EXIT_SUCCESS;
    }

    Serial.printf("Starting SD card format in background...\r\n");
    xTaskCreate(format_sd_task, "format_sd", 4096, NULL, 5, NULL);

    return EXIT_SUCCESS;
}
#endif

// Resolve a console argument to an absolute VFS path using the current directory.
static std::string resolve_path(const char *arg)
{
    if (arg[0] == '/') return arg;
    std::string pwd = getCurrentPath()->url;
    if (arg[0] == '.' && arg[1] == '\0') return pwd;
    return pwd + '/' + arg;
}

// ─── gzip ─────────────────────────────────────────────────────────────────────
static int cmd_gzip(int argc, char **argv)
{
    if (argc < 2) {
        Serial.printf("usage: gzip <source> [dest.gz]\r\n");
        return EXIT_FAILURE;
    }

    std::string src = resolve_path(argv[1]);
    std::string dst = (argc >= 3) ? resolve_path(argv[2]) : (src + ".gz");

    struct stat st = {};
    if (stat(src.c_str(), &st) != 0) {
        Serial.printf("gzip: '%s': %s\r\n", src.c_str(), strerror(errno));
        return EXIT_FAILURE;
    }
    size_t total = (size_t)st.st_size;

    FILE *in = fopen(src.c_str(), "rb");
    if (!in) {
        Serial.printf("gzip: cannot open '%s'\r\n", src.c_str());
        return EXIT_FAILURE;
    }

    gzFile gz = gzopen(dst.c_str(), "wb9");
    if (!gz) {
        Serial.printf("gzip: cannot create '%s'\r\n", dst.c_str());
        fclose(in);
        return EXIT_FAILURE;
    }

    const size_t kBufSz = 32768;
    char *buf = (char *)psram_malloc(kBufSz);
    if (!buf) {
        Serial.printf("gzip: out of memory\r\n");
        gzclose(gz);
        fclose(in);
        return EXIT_FAILURE;
    }

    Serial.printf("gzip: '%s' -> '%s' (%zu bytes)\r\n", src.c_str(), dst.c_str(), total);

    size_t written = 0, last_report = 0;
    const size_t kReport = 512 * 1024;
    size_t n;
    while ((n = fread(buf, 1, kBufSz, in)) > 0) {
        gzwrite(gz, buf, (unsigned)n);
        written += n;
        if (total > 0 && written - last_report >= kReport) {
            Serial.printf("  %zu / %zu bytes (%d%%)\r\n",
                          written, total, (int)(written * 100 / total));
            last_report = written;
        }
    }

    free(buf);
    gzclose(gz);
    fclose(in);
    if (total > 0 && last_report < written)
        Serial.printf("  %zu / %zu bytes (100%%)\r\n", written, total);
    Serial.printf("gzip: done\r\n");
    return EXIT_SUCCESS;
}

// ─── unzip ────────────────────────────────────────────────────────────────────
#ifndef MIN_CONFIG

static void unzip_mkdirs(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static int cmd_unzip(int argc, char **argv)
{
    if (argc < 2) {
        Serial.printf("usage: unzip <archive> [dest_folder]\r\n");
        return EXIT_FAILURE;
    }

    std::string src = resolve_path(argv[1]);
    std::string dest;
    if (argc >= 3) {
        dest = resolve_path(argv[2]);
    } else {
        size_t slash = src.rfind('/');
        dest = (slash != std::string::npos) ? src.substr(0, slash) : getCurrentPath()->url;
    }
    while (dest.size() > 1 && dest.back() == '/') dest.pop_back();

    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, src.c_str(), 16384) != ARCHIVE_OK) {
        Serial.printf("unzip: cannot open '%s': %s\r\n", src.c_str(), archive_error_string(a));
        archive_read_free(a);
        return EXIT_FAILURE;
    }

    uint8_t *buf = (uint8_t *)psram_malloc(4096);
    if (!buf) {
        Serial.printf("unzip: out of memory\r\n");
        archive_read_free(a);
        return EXIT_FAILURE;
    }

    struct archive_entry *entry;
    int count = 0, r;
    size_t total_bytes = 0;
    const size_t kProgressThreshold = 512 * 1024;
    const size_t kReport = 256 * 1024;

    while ((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        std::string path = dest + "/" + archive_entry_pathname(entry);
        unsigned int type = archive_entry_filetype(entry);

        int64_t entry_size = archive_entry_size_is_set(entry) ? archive_entry_size(entry) : -1;
        if (entry_size >= 0)
            Serial.printf("  %s  (%lld bytes)\r\n", path.c_str(), (long long)entry_size);
        else
            Serial.printf("  %s\r\n", path.c_str());

        if (type == AE_IFDIR) {
            unzip_mkdirs(path.c_str());
            archive_read_data_skip(a);
            continue;
        }

        if (type != AE_IFREG) {
            archive_read_data_skip(a);
            continue;
        }

        // Ensure parent directories exist
        size_t slash = path.rfind('/');
        if (slash != std::string::npos)
            unzip_mkdirs(path.substr(0, slash).c_str());

        FILE *f = fopen(path.c_str(), "wb");
        if (!f) {
            Serial.printf("unzip: cannot create '%s'\r\n", path.c_str());
            archive_read_data_skip(a);
            continue;
        }

        size_t entry_bytes = 0, last_report = 0;
        ssize_t n;
        while ((n = archive_read_data(a, buf, 4096)) > 0) {
            fwrite(buf, 1, (size_t)n, f);
            entry_bytes += (size_t)n;
            if (entry_size >= (int64_t)kProgressThreshold &&
                entry_bytes - last_report >= kReport) {
                Serial.printf("    %zu / %lld bytes (%d%%)\r\n",
                              entry_bytes, (long long)entry_size,
                              (int)(entry_bytes * 100 / (size_t)entry_size));
                last_report = entry_bytes;
            }
        }
        fclose(f);
        total_bytes += entry_bytes;
        count++;
    }

    free(buf);
    archive_read_close(a);
    archive_read_free(a);

    if (r != ARCHIVE_EOF) {
        Serial.printf("unzip: error: %s\r\n", archive_error_string(a));
        return EXIT_FAILURE;
    }

    Serial.printf("unzip: extracted %d entries, %zu bytes to '%s'\r\n",
                  count, total_bytes, dest.c_str());
    return EXIT_SUCCESS;
}
#endif // MIN_CONFIG

namespace ESP32Console::Commands
{
    const ConsoleCommand getCatCommand()
    {
        return ConsoleCommand("cat", &cat, "Show the content of one or more files.");
    }

    const ConsoleCommand getHexCommand()
    {
        return ConsoleCommand("hex", &hex, "Show the content of one or more files as hex.");
    }

    const ConsoleCommand getPWDCommand()
    {
        return ConsoleCommand("pwd", &pwd, "Show the current working dir");
    }

    const ConsoleCommand getCDCommand()
    {
        return ConsoleCommand("cd", &cd, "Change the working directory");
    }

    const ConsoleCommand getLsCommand()
    {
        return ConsoleCommand("ls", &ls, "List the contents of the given path");
    }

    const ConsoleCommand getMvCommand()
    {
        return ConsoleCommand("mv", &mv, "Move the given file to another place or name");
    }

    const ConsoleCommand getCPCommand()
    {
        return ConsoleCommand("cp", &cp, "Copy the given file to another place or name");
    }

    const ConsoleCommand getRMCommand()
    {
         return ConsoleCommand("rm", &rm, "Permanenty deletes the given file.");
    }

    const ConsoleCommand getRMDirCommand()
    {
        return ConsoleCommand("rmdir", &rmdir, "Permanenty deletes the given folder. Folder must be empty!");
    }

    const ConsoleCommand getMKDirCommand()
    {
        return ConsoleCommand("mkdir", &mkdir, "Create the DIRECTORY(ies), if they do not already exist.");
    }

    const ConsoleCommand getEditCommand()
    {
        return ConsoleCommand("edit", &ute, "Edit files");
    }

    const ConsoleCommand getMountCommand()
    {
        return ConsoleCommand("mount", &mount, "Mount url on device id");
    }

    const ConsoleCommand getAuthCommand()
    {
        return ConsoleCommand("auth", &auth, "Set username and password for current path");
    }

    const ConsoleCommand getWgetCommand()
    {
        return ConsoleCommand("wget", &wget, "Download url to file (-k skips TLS cert verification)");
    }

    const ConsoleCommand getUpdateCommand()
    {
        return ConsoleCommand("update", &update, "Update firmware from file on sd card");
    }

    const ConsoleCommand getDFCommand()
    {
        return ConsoleCommand("df", &df, "Show filesystem disk space usage");
    }

    const ConsoleCommand getEnableCommand()
    {
        return ConsoleCommand("enable", &enable, "Enable virtual drive");
    }
    const ConsoleCommand getDisableCommand()
    {
        return ConsoleCommand("disable", &disable, "Disable virtual drive");
    }

    const ConsoleCommand getGzipCommand()
    {
        return ConsoleCommand("gzip", &cmd_gzip, "Compress a file to .gz (level 9)");
    }

#ifndef MIN_CONFIG
    const ConsoleCommand getUnzipCommand()
    {
        return ConsoleCommand("unzip", &cmd_unzip, "Extract an archive to a folder");
    }
#endif

#ifdef SD_CARD
    const ConsoleCommand getFormatSDCommand()
    {
        return ConsoleCommand("format_sd", &format_sd, "Format the SD card (use -y to confirm)");
    }

    const ConsoleCommand getUpdatedbCommand()
    {
        return ConsoleCommand("updatedb", &updatedb, "Build the locate database from the SD card");
    }

    const ConsoleCommand getLocateCommand()
    {
        return ConsoleCommand("locate", &locate, "Search the locate database for files matching a pattern");
    }
#endif
}