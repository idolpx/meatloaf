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
    if (argc == 1)
    {
        listPath = MFSOwner::File(getCurrentPath()->url);
    }
    else if (argc == 2)
    {
        listPath = getCurrentPath()->cd(argv[1]);
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
    if (argc != 2)
    {
        Serial.printf("wget {url}\r\n");
        return EXIT_SUCCESS;
    }

    std::string pwd = getCurrentPath()->url;

    std::unique_ptr<MFile>f(MFSOwner::File(argv[1]));
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
#include <vector>
#include <ctime>

#include "string_utils.h"

#define LOCATE_DB_PATH "/sd/.locate"

/* Volatile scan state — written by the scan task, read by locate/updatedb. */
static volatile int    s_scan_running = 0;
static volatile int    s_scan_files   = 0;
static volatile int    s_scan_dirs    = 0;
static volatile int    s_scan_errors  = 0;
static volatile time_t s_scan_start   = 0;
static volatile time_t s_scan_end     = 0;

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

static void updatedb_scan(sqlite3 *db, sqlite3_stmt *stmt)
{
    std::vector<PsramPath> dirs;
    dirs.emplace_back("/sd");

    char full[PATH_MAX];
    int  batch = 0;

    while (!dirs.empty()) {
        PsramPath cur(std::move(dirs.back()));
        dirs.pop_back();
        if (!cur.s) continue;

        DIR *d = opendir(cur.s);
        if (!d) continue;

        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr) {
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

            const char *rel = full + 3;  // relative to /sd

            struct stat st;
            if (stat(full, &st) != 0) continue;

            int is_dir = S_ISDIR(st.st_mode) ? 1 : 0;

            sqlite3_reset(stmt);
            sqlite3_bind_text(stmt, 1, rel, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, (sqlite3_int64)st.st_size);
            sqlite3_bind_int64(stmt, 3, (sqlite3_int64)st.st_mtime);
            sqlite3_bind_int(stmt, 4, is_dir);
            int rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                s_scan_errors = s_scan_errors + 1;
                if (s_scan_errors <= 3)
                    Serial.printf("  insert error %d: %s — %s\r\n",
                                  rc, sqlite3_errmsg(db), rel);
            }

            if (is_dir) {
                s_scan_dirs = s_scan_dirs + 1;
                dirs.emplace_back(full);
            } else {
                s_scan_files = s_scan_files + 1;
            }

            // Commit every 1000 rows. journal_mode=DELETE resets the pager
            // state on each COMMIT/BEGIN cycle via the journal file lifecycle.
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
                    // auto-commit fallback: rows still saved one-by-one
                }
                batch = 0;
            }

            int total = s_scan_files + s_scan_dirs;
            if (total % 100 == 0)
                Serial.printf("  %d files, %d directories indexed...\r\n",
                              (int)s_scan_files, (int)s_scan_dirs);
        }
        closedir(d);
        vTaskDelay(1);  // yield after each directory so SDMMC DMA can complete
    }
}

static void updatedb_task(void *arg)
{
    // SQLITE_OMIT_AUTOINIT: must call sqlite3_initialize() before sqlite3_open().
    sqlite3_initialize();

    // Remove any stale/corrupt database from a previous interrupted scan.
    unlink(LOCATE_DB_PATH);

    sqlite3 *db = nullptr;
    if (sqlite3_open(LOCATE_DB_PATH, &db) != SQLITE_OK) {
        Serial.printf("updatedb: cannot create database: %s\r\n",
                      db ? sqlite3_errmsg(db) : "out of memory");
        if (db) sqlite3_close(db);
        s_scan_running = 0;
        vTaskDelete(NULL);
        return;
    }

    // DELETE journal (the SQLite default) writes the rollback journal to SD and
    // deletes it on each COMMIT.  This properly resets the pager's internal
    // state on every COMMIT/BEGIN cycle, fixing the "cannot commit - no
    // transaction is active" error that journal_mode=OFF has after 2+ cycles
    // with SQLITE_DEFAULT_LOCKING_MODE=1 (EXCLUSIVE).
    // MEMORY journal was tried but rejected: it allocates ~520-byte entries in
    // internal DRAM (below SPIRAM_MALLOC_ALWAYSINTERNAL threshold), exhausting
    // the DMA-capable heap and breaking SDMMC writes at row ~300.
    sqlite3_exec(db, "PRAGMA journal_mode = DELETE", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous = OFF", nullptr, nullptr, nullptr);
    // 32 pages × 512 bytes = 16 KB; keeps internal DRAM pressure low so the
    // SDMMC DMA allocator always has its 125-byte window available.
    sqlite3_exec(db, "PRAGMA cache_size = 32", nullptr, nullptr, nullptr);

    const char *schema =
        "DROP TABLE IF EXISTS files;"
        "CREATE TABLE files ("
        "  path TEXT PRIMARY KEY,"
        "  size INTEGER NOT NULL DEFAULT 0,"
        "  mtime INTEGER NOT NULL DEFAULT 0,"
        "  is_dir INTEGER NOT NULL DEFAULT 0"
        ");";
    // No explicit idx_path: "path TEXT PRIMARY KEY" already creates a unique
    // B-tree index on path.  A second index doubles write work per INSERT.

    char *errmsg = nullptr;
    if (sqlite3_exec(db, schema, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        Serial.printf("updatedb: %s\r\n", errmsg);
        sqlite3_free(errmsg);
        sqlite3_close(db);
        s_scan_running = 0;
        vTaskDelete(NULL);
        return;
    }

    const char *sql =
        "INSERT OR REPLACE INTO files (path, size, mtime, is_dir) VALUES (?, ?, ?, ?)";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Serial.printf("updatedb: prepare failed: %s\r\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        s_scan_running = 0;
        vTaskDelete(NULL);
        return;
    }

    s_scan_errors = 0;
    char *begin_err = nullptr;
    if (sqlite3_exec(db, "BEGIN", nullptr, nullptr, &begin_err) != SQLITE_OK) {
        Serial.printf("updatedb: initial BEGIN failed: %s\r\n",
                      begin_err ? begin_err : sqlite3_errmsg(db));
        sqlite3_free(begin_err);
    }
    updatedb_scan(db, stmt);

    // Commit whatever the last partial batch left in-transaction.
    if (!sqlite3_get_autocommit(db)) {
        char *commit_err = nullptr;
        if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, &commit_err) != SQLITE_OK) {
            Serial.printf("updatedb: final COMMIT failed: %s\r\n",
                          commit_err ? commit_err : sqlite3_errmsg(db));
            sqlite3_free(commit_err);
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    s_scan_end = time(nullptr);
    s_scan_running = 0;

    Serial.printf("updatedb: done — %d files, %d directories, %d errors, %ld seconds.\r\n",
                  (int)s_scan_files, (int)s_scan_dirs,
                  (int)s_scan_errors, (long)(s_scan_end - s_scan_start));
    vTaskDelete(NULL);
}

int updatedb(int argc, char **argv)
{
    if (!fnSDFAT.running()) {
        Serial.printf("updatedb: SD card not mounted\r\n");
        return EXIT_FAILURE;
    }
    if (s_scan_running) {
        Serial.printf("updatedb: scan already in progress (%d files, %d directories so far)\r\n",
                      (int)s_scan_files, (int)s_scan_dirs);
        return EXIT_FAILURE;
    }

    s_scan_files   = 0;
    s_scan_dirs    = 0;
    s_scan_errors  = 0;
    s_scan_start   = time(nullptr);
    s_scan_end     = 0;
    s_scan_running = 1;

    Serial.printf("Building locate database in background...\r\n");
    xTaskCreate(updatedb_task, "updatedb", 8192, nullptr, 5, nullptr);
    return EXIT_SUCCESS;
}

int locate(int argc, char **argv)
{
    if (argc < 2) {
        if (s_scan_running) {
            time_t elapsed = time(nullptr) - s_scan_start;
            Serial.printf("Scan in progress: %d files, %d directories indexed (%ld seconds elapsed)\r\n",
                          (int)s_scan_files, (int)s_scan_dirs, (long)elapsed);
        } else if (s_scan_end > 0) {
            Serial.printf("Last scan: %d files, %d directories in %ld seconds\r\n",
                          (int)s_scan_files, (int)s_scan_dirs,
                          (long)(s_scan_end - s_scan_start));
        } else {
            /* No scan this session — query the database for persisted totals. */
            if (!fnSDFAT.running()) {
                Serial.printf("locate: SD card not mounted\r\n");
                return EXIT_FAILURE;
            }
            sqlite3_initialize();
            sqlite3 *db = nullptr;
            if (sqlite3_open_v2(LOCATE_DB_PATH, &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
                Serial.printf("No index found. Run 'updatedb' to build the database.\r\n");
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
            Serial.printf("Index contains %d files in %d directories. Run 'updatedb' to refresh.\r\n",
                          files, dirs);
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

    sqlite3_initialize();  // SQLITE_OMIT_AUTOINIT: required before sqlite3_open

    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(LOCATE_DB_PATH, &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        Serial.printf("locate: database not found. Run 'updatedb' first.\r\n");
        if (db) sqlite3_close(db);
        return EXIT_FAILURE;
    }

    // Convert shell wildcards (* → %, ? → _) to SQL LIKE syntax.
    // If the user provided no wildcards at all, wrap the string in % for substring search.
    bool has_wildcards = strchr(argv[1], '*') || strchr(argv[1], '?');
    std::string pattern(argv[1]);
    for (char &c : pattern) {
        if (c == '*') c = '%';
        else if (c == '?') c = '_';
    }
    if (!has_wildcards)
        pattern = "%" + pattern + "%";

    const char *sql =
        "SELECT path, size, is_dir FROM files WHERE path LIKE ? ORDER BY path";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Serial.printf("locate: %s\r\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return EXIT_FAILURE;
    }

    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *path = (const char *)sqlite3_column_text(stmt, 0);
        sqlite3_int64 size = sqlite3_column_int64(stmt, 1);
        int is_dir = sqlite3_column_int(stmt, 2);
        if (path)
            Serial.printf("%c %8lld  /sd%s\r\n", is_dir ? 'd' : '-', (long long)size, path);
        count++;
    }

    sqlite3_finalize(stmt);
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
        return ConsoleCommand("wget", &wget, "Download url to file");
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