// Reading existing REL files out of D64-family images. Fixtures are made by
// VICE's c1541 so the side-sector layout is not our own reading of it.
#include <unity.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "media/disk/d64.h"
#include "media/disk/d71.h"
#include "media/disk/d80.h"
#include "media/disk/d81.h"
#include "media/disk/d82.h"
#include "media/hd/dnp.h"
#include "media/file/rel.h"
#include "../test_disk_write/file_container_stream.h"
#include "../test_disk_write/c1541_oracle.h"
#include "../test_disk_write/image_invariants.h"

static const char* kHost = "build_rel_host.bin";
static const char* kD64 = "build_rel.d64";
static const char* kD81 = "build_rel.d81";
static const char* kOut = "build_rel_out.bin";
static const char* kImg = "build_rel_w.img";

void setUp(void) {}
void tearDown(void)
{
    remove(kHost); remove(kD64); remove(kD81); remove(kOut); remove(kImg);
}

static std::vector<uint8_t> pattern(uint32_t records, uint32_t len)
{
    std::vector<uint8_t> v(records * len);
    for (uint32_t r = 0; r < records; r++)
        for (uint32_t i = 0; i < len; i++)
            v[r * len + i] = (uint8_t)(r * 7 + i * 3 + 1);   // never a long zero run
    return v;
}

static void make_image(const char* type, const char* path, const std::vector<uint8_t>& data, uint32_t len)
{
    FILE* fp = fopen(kHost, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_UINT32(data.size(), (uint32_t)fwrite(data.data(), 1, data.size(), fp));
    fclose(fp);
    int rc = 0;
    std::string out = c1541_run("-format " + c1541_quote("t,01") + " " + type + " " + c1541_quote(path) +
                                " -write " + c1541_quote(kHost) + " " + c1541_quote("data,l," + std::to_string(len)), &rc);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, out.c_str());
    TEST_ASSERT_TRUE_MESSAGE(out.find("REL file") != std::string::npos, out.c_str());
}

// Opens DATA from the image, checks the record length, and compares every byte
// the storage serves against both the source pattern and what c1541 itself
// reads back out of the image.
static void check(D64MStream& image, const char* path, const std::vector<uint8_t>& data, uint32_t len)
{
    image.mode = std::ios_base::in;
    TEST_ASSERT_TRUE(image.seekPath("data"));
    TEST_ASSERT_EQUAL_UINT8(len, image.relRecordLength());

    auto st = image.relStorage();
    TEST_ASSERT_NOT_NULL(st.get());

    // c1541 pads a REL file out to whole side-sector-allocated blocks, so the
    // truth for the size is what c1541 itself reads back, not data.size().
    size_t n = 0;
    TEST_ASSERT_TRUE(c1541_read(path, "data,l", kOut, &n));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, st->size());
    TEST_ASSERT_TRUE(st->size() >= data.size());

    std::vector<uint8_t> got(st->size());
    TEST_ASSERT_EQUAL_UINT32(got.size(), st->readAt(0, got.data(), (uint32_t)got.size()));
    TEST_ASSERT_TRUE(std::equal(data.begin(), data.end(), got.begin()));

    // The whole file matches c1541's own read-back, not just the part we wrote.
    std::string ref;
    TEST_ASSERT_TRUE(c1541_read_raw_file(kOut, ref));
    TEST_ASSERT_EQUAL_MEMORY(ref.data(), got.data(), got.size());

    // Offsets that straddle block boundaries read the same bytes.
    for (uint32_t off : { 253u, 254u, 507u })
        if (off + 10 <= got.size())
        {
            uint8_t b[10];
            TEST_ASSERT_EQUAL_UINT32(10, st->readAt(off, b, 10));
            TEST_ASSERT_EQUAL_MEMORY(&got[off], b, 10);
        }
    uint8_t one;
    TEST_ASSERT_EQUAL_UINT32(0, st->readAt(st->size(), &one, 1));
}

void test_rel_d64_len10_three_records(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    auto data = pattern(3, 10);
    make_image("d64", kD64, data, 10);
    auto image_p = std::make_shared<D64MStream>(std::make_shared<FileContainerStream>(kD64)); D64MStream& image = *image_p;
    check(image, kD64, data, 10);
}

void test_rel_d64_len254_crosses_side_sector_group(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    auto data = pattern(130, 254);   // 130 data blocks > 120 per side sector
    make_image("d64", kD64, data, 254);
    auto image_p = std::make_shared<D64MStream>(std::make_shared<FileContainerStream>(kD64)); D64MStream& image = *image_p;
    check(image, kD64, data, 254);
}

void test_rel_d81_len100_needs_second_super_side_group(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    auto data = pattern(1900, 100);  // 190000 bytes = 748 blocks > 720 (6 side sectors)
    make_image("d81", kD81, data, 100);
    auto image_p = std::make_shared<D81MStream>(std::make_shared<FileContainerStream>(kD81)); D64MStream& image = *image_p;
    check(image, kD81, data, 100);
}

void test_rel_record_length_zero_for_non_rel(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    std::vector<uint8_t> data(300, 0x41);
    FILE* fp = fopen(kHost, "wb"); fwrite(data.data(), 1, data.size(), fp); fclose(fp);
    c1541_run("-format " + c1541_quote("t,01") + " d64 " + c1541_quote(kD64) + " -write " + c1541_quote(kHost) + " data");
    auto image_p = std::make_shared<D64MStream>(std::make_shared<FileContainerStream>(kD64)); D64MStream& image = *image_p;
    image.mode = std::ios_base::in;
    TEST_ASSERT_TRUE(image.seekPath("data"));
    TEST_ASSERT_EQUAL_UINT8(0, image.relRecordLength());
    TEST_ASSERT_NULL(image.relStorage().get());
}

// The storage must keep the stream alive: drop every other reference and read.
void test_rel_storage_outlives_stream_owner(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    auto data = pattern(3, 10);
    make_image("d64", kD64, data, 10);
    std::unique_ptr<RELStorage> st;
    {
        auto image = std::make_shared<D64MStream>(std::make_shared<FileContainerStream>(kD64));
        image->mode = std::ios_base::in;
        TEST_ASSERT_TRUE(image->seekPath("data"));
        st = image->relStorage();
        TEST_ASSERT_NOT_NULL(st.get());
    }   // last external reference gone
    RELMStream rel(std::move(st), 10);
    uint8_t buf[10];
    TEST_ASSERT_EQUAL_UINT8(0, rel.position(2, 1));
    TEST_ASSERT_EQUAL_UINT32(10, rel.read(buf, 10));
    TEST_ASSERT_EQUAL_MEMORY(&data[10], buf, 10);
}


/********************************************************
 * Creating, growing and scratching (Task 7)
 ********************************************************/

static const uint8_t ST_OK_ = 0;
static const uint8_t ST_FILE_TOO_LARGE_ = 52;
static const uint8_t ST_DISK_FULL_ = 72;

struct RelFmt {
    const char* name;
    const char* c1541_type;   // nullptr: no c1541 oracle (DNP)
    uint32_t size;
    std::shared_ptr<D64MStream> (*make)(std::shared_ptr<MStream>);
    uint32_t cross;           // records of len 254 that cross a side-sector (group) boundary
};

static std::shared_ptr<D64MStream> mk_d64(std::shared_ptr<MStream> s) { return std::make_shared<D64MStream>(s); }
static std::shared_ptr<D64MStream> mk_d71(std::shared_ptr<MStream> s) { return std::make_shared<D71MStream>(s); }
static std::shared_ptr<D64MStream> mk_d81(std::shared_ptr<MStream> s) { return std::make_shared<D81MStream>(s); }
static std::shared_ptr<D64MStream> mk_dnp(std::shared_ptr<MStream> s) { return std::make_shared<DNPMStream>(s); }
static std::shared_ptr<D64MStream> mk_d80(std::shared_ptr<MStream> s) { return std::make_shared<D80MStream>(s); }
static std::shared_ptr<D64MStream> mk_d82(std::shared_ptr<MStream> s) { return std::make_shared<D82MStream>(s); }

// Formats exercised only by the limit tests below (kFmts is the common set).
static const RelFmt kD80 = { "d80", "d80", 533248,  mk_d80, 0 };
static const RelFmt kD82 = { "d82", "d82", 1066496, mk_d82, 0 };

// D64/D71 cross from the first side sector into the second (>120 blocks);
// D81/DNP cross from the first super-side-sector group into the second (>720).
static const RelFmt kFmts[] = {
    { "d64", "d64", 174848,  mk_d64, 200 },
    { "d71", "d71", 349696,  mk_d71, 200 },
    { "d81", "d81", 819200,  mk_d81, 800 },
    { "dnp", nullptr, 4 * 65536, mk_dnp, 800 },
};

static void fresh_image(const RelFmt& f)
{
    remove(kImg);
    if (f.c1541_type)
    {
        int rc = 0;
        std::string out = c1541_run("-format " + c1541_quote("t,01") + " " + f.c1541_type + " " + kImg, &rc);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, out.c_str());
        return;
    }
    auto src = std::make_shared<FileContainerStream>(kImg, f.size);
    auto img = f.make(src);
    TEST_ASSERT_TRUE_MESSAGE(img->formatImage("t", "01"), f.name);
}

static std::shared_ptr<D64MStream> open_rw(const RelFmt& f)
{
    auto img = f.make(std::make_shared<FileContainerStream>(kImg));
    img->mode = std::ios_base::in | std::ios_base::out;
    return img;
}

static uint16_t free_blocks(const RelFmt& f)
{
    auto img = f.make(std::make_shared<FileContainerStream>(kImg));
    img->mode = std::ios_base::in;
    return img->blocksFree();
}

static std::string slurp(const char* path)
{
    std::string s;
    TEST_ASSERT_TRUE(c1541_read_raw_file(path, s));
    return s;
}

// Writes records [first, first+n) of 'data' (len bytes each) through a
// RELMStream; returns the status of the first failing write, or 0.
static uint8_t write_records(RELMStream& rel, const std::vector<uint8_t>& data, uint32_t len,
                             uint32_t first, uint32_t n)
{
    for (uint32_t r = first; r < first + n; r++)
    {
        // Positioning past the end answers 50 RECORD NOT PRESENT, as on a real
        // drive; the write then grows the file and leaves that status alone.
        rel.position((uint16_t)(r + 1), 1);
        rel.write(&data[r * len], len);
        if (rel.status() != ST_OK_ && rel.status() != 50) return rel.status();
        rel.endRecord();
    }
    return ST_OK_;
}

// vdrive_rel_open_new: a new REL entry, then records written through it.
static void create_rel(const RelFmt& f, const std::vector<uint8_t>& data, uint32_t len, uint32_t records)
{
    auto img = open_rw(f);
    auto st = img->createRel("data", (uint8_t)len);
    TEST_ASSERT_NOT_NULL_MESSAGE(st.get(), f.name);
    RELMStream rel(std::move(st), (uint8_t)len);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ST_OK_, write_records(rel, data, len, 0, records), f.name);
    rel.close();
}

// Reopens an existing REL and writes record 'record' (1-based); returns status.
static uint8_t write_one(const RelFmt& f, const std::vector<uint8_t>& data, uint32_t len, uint32_t record)
{
    auto img = open_rw(f);
    TEST_ASSERT_TRUE_MESSAGE(img->seekPath("data"), f.name);
    auto st = img->relStorage();
    TEST_ASSERT_NOT_NULL_MESSAGE(st.get(), f.name);
    RELMStream rel(std::move(st), (uint8_t)len);
    uint8_t s = write_records(rel, data, len, record - 1, 1);
    rel.close();
    return s;
}

// The image is sound, lists the file as REL, and the first expect.size() bytes
// of the file - read back by c1541 AND by a fresh stream of ours - are expect.
static void check_rel(const RelFmt& f, const std::vector<uint8_t>& expect, uint32_t len)
{
    {
        auto img = f.make(std::make_shared<FileContainerStream>(kImg));
        InvariantResult r = check_invariants(*img);
        TEST_ASSERT_TRUE_MESSAGE(r.ok, (std::string(f.name) + ": " + r.message).c_str());
    }
    {
        auto img = f.make(std::make_shared<FileContainerStream>(kImg));
        img->mode = std::ios_base::in;
        TEST_ASSERT_TRUE_MESSAGE(img->seekPath("data"), f.name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(len, img->relRecordLength(), f.name);
        auto st = img->relStorage();
        TEST_ASSERT_NOT_NULL_MESSAGE(st.get(), f.name);
        TEST_ASSERT_TRUE_MESSAGE(st->size() >= expect.size(), f.name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, st->size() % len, f.name);   // VICE fills whole records
        std::vector<uint8_t> got(expect.size());
        TEST_ASSERT_EQUAL_UINT32(got.size(), st->readAt(0, got.data(), (uint32_t)got.size()));
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expect.data(), got.data(), got.size(), f.name);
    }
    if (!f.c1541_type) return;
    std::string dir = c1541_dir(kImg);
    TEST_ASSERT_TRUE_MESSAGE(dir.find("rel") != std::string::npos, dir.c_str());
    size_t n = 0;
    TEST_ASSERT_TRUE_MESSAGE(c1541_read(kImg, "data,l", kOut, &n), f.name);
    TEST_ASSERT_TRUE_MESSAGE(n >= expect.size(), f.name);
    std::string ref = slurp(kOut);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expect.data(), ref.data(), expect.size(), f.name);
    // c1541's validate rewrites the BAM and REL block counts silently; an
    // unchanged image means both were already right.
    TEST_ASSERT_TRUE_MESSAGE(c1541_validate(kImg), f.name);
}

// What a grown file reads as: vdrive_rel_grow fills new records with $FF,0...
static std::vector<uint8_t> filled(uint32_t records, uint32_t len)
{
    std::vector<uint8_t> v(records * len, 0);
    for (uint32_t r = 0; r < records; r++) v[r * len] = 0xFF;
    return v;
}

// vdrive_rel_open_new + vdrive_rel_add_sector (first data block, side sector,
// super side sector on D81/DNP) + vdrive_rel_write.
void test_rel_create_and_write_five_records(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    for (const RelFmt& f : kFmts)
    {
        fresh_image(f);
        auto data = pattern(5, 20);
        create_rel(f, data, 20, 5);
        check_rel(f, data, 20);
    }
}

// vdrive_rel_grow: positioning past the end and writing adds blocks until the
// record exists; the records in between are $FF,0...
void test_rel_grow_five_to_thousand(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    for (const RelFmt& f : kFmts)
    {
        fresh_image(f);
        auto data = pattern(1000, 20);
        create_rel(f, data, 20, 5);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(ST_OK_, write_one(f, data, 20, 1000), f.name);
        auto expect = filled(1000, 20);
        std::copy(data.begin(), data.begin() + 5 * 20, expect.begin());
        std::copy(data.begin() + 999 * 20, data.end(), expect.begin() + 999 * 20);
        check_rel(f, expect, 20);
    }
}

// vdrive_rel_add_sector: a full side sector (120 data blocks) starts a new one;
// on a 1581/CMD native a full group of six starts a new group in the super
// side sector.
void test_rel_grow_crosses_side_sector_groups(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    for (const RelFmt& f : kFmts)
    {
        fresh_image(f);
        auto data = pattern(f.cross, 254);
        create_rel(f, data, 254, 5);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(ST_OK_, write_one(f, data, 254, f.cross), f.name);
        auto expect = filled(f.cross, 254);
        std::copy(data.begin(), data.begin() + 5 * 254, expect.begin());
        std::copy(data.end() - 254, data.end(), expect.end() - 254);
        check_rel(f, expect, 254);
    }
}

// vdrive_rel_scratch: the data chain and every side sector (and the super side
// sector) go back to the BAM.
void test_rel_scratch_frees_everything(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    for (const RelFmt& f : kFmts)
    {
        fresh_image(f);
        uint16_t before = free_blocks(f);
        auto data = pattern(f.cross, 254);
        create_rel(f, data, 254, f.cross);
        TEST_ASSERT_TRUE_MESSAGE(free_blocks(f) < before, f.name);
        {
            auto img = open_rw(f);
            TEST_ASSERT_TRUE_MESSAGE(img->removeFile("data"), f.name);
        }
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(before, free_blocks(f), f.name);
        auto img = f.make(std::make_shared<FileContainerStream>(kImg));
        InvariantResult r = check_invariants(*img);
        TEST_ASSERT_TRUE_MESSAGE(r.ok, (std::string(f.name) + ": " + r.message).c_str());
        if (f.c1541_type) TEST_ASSERT_TRUE_MESSAGE(c1541_validate(kImg), f.name);
    }
}

// vdrive_rel_blocks_max: a 1541/1571 REL file is capped at 700 data blocks +
// 6 side sectors. A 35-track D64 has only 664 free blocks, so it is DISK FULL
// long before that - the 1541 limit is checked on a D71, which shares it.
void test_rel_1541_too_large_is_52_and_changes_nothing(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    const RelFmt& f = kFmts[1];   // d71
    fresh_image(f);
    auto data = pattern(701, 254);
    create_rel(f, data, 254, 700);   // 700 data blocks: the limit, allowed
    std::string before = slurp(kImg);
    TEST_ASSERT_EQUAL_UINT8(ST_FILE_TOO_LARGE_, write_one(f, data, 254, 701));
    TEST_ASSERT_TRUE(before == slurp(kImg));
    data.resize(700 * 254);
    check_rel(f, data, 254);
}

// vdrive_rel_add_sector: no free block is 72 DISK FULL; nothing claimed for
// the failed grow stays allocated.
void test_rel_d64_disk_full_is_72_and_leaks_nothing(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    const RelFmt& f = kFmts[0];
    fresh_image(f);
    auto data = pattern(20, 254);
    create_rel(f, data, 254, 5);
    // Fill all but three blocks with a PRG.
    {
        uint16_t left = free_blocks(f);
        auto img = f.make(std::make_shared<FileContainerStream>(kImg));
        img->mode = std::ios_base::out;
        TEST_ASSERT_TRUE(img->seekPath("filler"));
        std::vector<uint8_t> fill((size_t)(left - 3) * 254, 0x5A);
        TEST_ASSERT_EQUAL_UINT32(fill.size(), img->write(fill.data(), (uint32_t)fill.size()));
        img->close();
        TEST_ASSERT_EQUAL_INT(0, img->error());
    }
    TEST_ASSERT_EQUAL_UINT16(3, free_blocks(f));
    std::string before = slurp(kImg);
    TEST_ASSERT_EQUAL_UINT8(ST_DISK_FULL_, write_one(f, data, 254, 20));   // needs 15 more
    TEST_ASSERT_TRUE(before == slurp(kImg));
    data.resize(5 * 254);
    check_rel(f, data, 254);
}

// Fills the image with a PRG until exactly 'leave' blocks are free.
static void fill_leaving(const RelFmt& f, uint16_t leave)
{
    uint16_t left = free_blocks(f);
    TEST_ASSERT_TRUE_MESSAGE(left > leave, f.name);
    auto img = f.make(std::make_shared<FileContainerStream>(kImg));
    img->mode = std::ios_base::out;
    TEST_ASSERT_TRUE(img->seekPath("filler"));
    std::vector<uint8_t> fill((size_t)(left - leave) * 254, 0x5A);
    TEST_ASSERT_EQUAL_UINT32(fill.size(), img->write(fill.data(), (uint32_t)fill.size()));
    img->close();
    TEST_ASSERT_EQUAL_INT(0, img->error());
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(leave, free_blocks(f), f.name);
}

// A record of 254 bytes is one data block, so 'records' of them is that many
// data blocks. At the format's limit the file is accepted; one block more is
// 52 FILE TOO LARGE and the image is left byte-identical.
static void check_too_large(const RelFmt& f, uint32_t max_data_blocks)
{
    fresh_image(f);
    auto data = pattern(max_data_blocks + 1, 254);
    create_rel(f, data, 254, 5);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ST_OK_, write_one(f, data, 254, max_data_blocks), f.name);
    std::string before = slurp(kImg);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ST_FILE_TOO_LARGE_, write_one(f, data, 254, max_data_blocks + 1), f.name);
    TEST_ASSERT_TRUE_MESSAGE(before == slurp(kImg), f.name);
    auto expect = filled(max_data_blocks, 254);
    std::copy(data.begin(), data.begin() + 5 * 254, expect.begin());
    std::copy(data.begin() + (max_data_blocks - 1) * 254, data.begin() + max_data_blocks * 254,
              expect.begin() + (max_data_blocks - 1) * 254);
    check_rel(f, expect, 254);
}

// vdrive_rel_blocks_max for an 8050: 720 data blocks + 6 side sectors, and no
// super side sector to go further.
void test_rel_d80_too_large_is_52_and_changes_nothing(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    check_too_large(kD80, 720);
}

// vdrive_rel_blocks_max for an 8250: 4090 data blocks, their side sectors and
// the super side sector.
void test_rel_d82_too_large_is_52_and_changes_nothing(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    check_too_large(kD82, 4090);
}

// Growing past a side sector's 120 data blocks needs TWO blocks - the data
// block and the new side sector. With room for only the first the grow is 72
// DISK FULL and claims nothing, rather than writing a data block that no side
// sector points at.
void test_rel_side_sector_allocation_failure_changes_nothing(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    const RelFmt& f = kFmts[0];   // d64
    fresh_image(f);
    auto data = pattern(121, 254);
    create_rel(f, data, 254, 120);   // exactly one full side sector
    fill_leaving(f, 1);
    std::string before = slurp(kImg);
    TEST_ASSERT_EQUAL_UINT8(ST_DISK_FULL_, write_one(f, data, 254, 121));
    TEST_ASSERT_TRUE(before == slurp(kImg));
    data.resize(120 * 254);
    check_rel(f, data, 254);
}

// The same at a super-side-sector group boundary: 720 data blocks fill the six
// side sectors of group 0, so the 721st needs the first side sector of group 1.
void test_rel_super_group_allocation_failure_changes_nothing(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    const RelFmt& f = kFmts[2];   // d81
    fresh_image(f);
    auto data = pattern(721, 254);
    create_rel(f, data, 254, 720);
    fill_leaving(f, 1);
    std::string before = slurp(kImg);
    TEST_ASSERT_EQUAL_UINT8(ST_DISK_FULL_, write_one(f, data, 254, 721));
    TEST_ASSERT_TRUE(before == slurp(kImg));
    data.resize(720 * 254);
    check_rel(f, data, 254);
}

// vdrive_rel_open (newrelfile) -> vdrive_rel_grow(.., 0): what
// D64MFile::createRelStream does after createRel(). Growing to one record
// takes one data block, and a REL file is whole blocks of records (254 / 20 =
// 12), each $FF then zeros; c1541 reads it the same way and validate leaves
// it alone.
void test_rel_new_file_has_one_record(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    for (const RelFmt& f : kFmts)
    {
        fresh_image(f);
        {
            auto img = open_rw(f);
            auto st = img->createRel("data", 20);
            TEST_ASSERT_NOT_NULL_MESSAGE(st.get(), f.name);
            RELMStream rel(std::move(st), 20);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(ST_OK_, rel.growTo(1), f.name);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(254 / 20, rel.recordCount(), f.name);
            rel.close();
        }
        check_rel(f, filled(254 / 20, 20), 20);
    }
}

// Forwards to a real storage and counts the writes made through it.
struct CountingStorage : public RELStorage {
    std::unique_ptr<RELStorage> inner;
    uint32_t writes = 0;
    explicit CountingStorage(std::unique_ptr<RELStorage> s) : inner(std::move(s)) {}
    uint32_t size() override { return inner->size(); }
    uint32_t readAt(uint32_t o, uint8_t* b, uint32_t n) override { return inner->readAt(o, b, n); }
    uint32_t writeAt(uint32_t o, const uint8_t* b, uint32_t n) override { writes++; return inner->writeAt(o, b, n); }
    uint8_t grow(uint32_t n) override { return inner->grow(n); }
    bool writable() override { return inner->writable(); }
    bool fillsNewRecords() const override { return inner->fillsNewRecords(); }
};

// An image's grow writes the $FF,0... fill itself (writeDataBlocks), so
// RELMStream::growTo must not write it a second time record by record - and
// the records it grew must still read as $FF then zeros, across many blocks.
void test_rel_grow_fill_is_written_once_by_the_image(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    for (const RelFmt& f : kFmts)
    {
        fresh_image(f);
        {
            auto img = open_rw(f);
            auto st = img->createRel("data", 20);
            TEST_ASSERT_NOT_NULL_MESSAGE(st.get(), f.name);
            TEST_ASSERT_TRUE_MESSAGE(st->fillsNewRecords(), f.name);
            auto counting = std::unique_ptr<CountingStorage>(new CountingStorage(std::move(st)));
            CountingStorage* c = counting.get();
            RELMStream rel(std::move(counting), 20);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(ST_OK_, rel.growTo(300), f.name);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, c->writes, f.name);
            TEST_ASSERT_TRUE_MESSAGE(rel.recordCount() >= 300, f.name);
            rel.close();
        }
        check_rel(f, filled(300, 20), 20);
    }
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_rel_d64_len10_three_records);
    RUN_TEST(test_rel_d64_len254_crosses_side_sector_group);
    RUN_TEST(test_rel_d81_len100_needs_second_super_side_group);
    RUN_TEST(test_rel_record_length_zero_for_non_rel);
    RUN_TEST(test_rel_storage_outlives_stream_owner);
    RUN_TEST(test_rel_create_and_write_five_records);
    RUN_TEST(test_rel_new_file_has_one_record);
    RUN_TEST(test_rel_grow_fill_is_written_once_by_the_image);
    RUN_TEST(test_rel_grow_five_to_thousand);
    RUN_TEST(test_rel_grow_crosses_side_sector_groups);
    RUN_TEST(test_rel_scratch_frees_everything);
    RUN_TEST(test_rel_1541_too_large_is_52_and_changes_nothing);
    RUN_TEST(test_rel_d64_disk_full_is_72_and_leaks_nothing);
    RUN_TEST(test_rel_d80_too_large_is_52_and_changes_nothing);
    RUN_TEST(test_rel_d82_too_large_is_52_and_changes_nothing);
    RUN_TEST(test_rel_side_sector_allocation_failure_changes_nothing);
    RUN_TEST(test_rel_super_group_allocation_failure_changes_nothing);
    return UNITY_END();
}
