// Read-path tests for the G64 GCR bitstream format.
//
// These exist because three defects were fixed in g64.cpp and none of them was
// reachable by any existing test - there is no .g64 anywhere in .data/media, and
// the firmware has a GCR decoder but no encoder, so it cannot produce one
// either. The fixture is therefore generated from a real .d64 by
// host/make_g64.py; the tests skip cleanly when it has not been run.
//
// What is being pinned, in order of how badly it used to break:
//
//   1. readContainer() indexed sector_buffer by _position - the position in the
//      FILE - so every block of a file after the first read past the end of a
//      260-byte buffer and returned adjacent memory as file content.
//   2. seekSector() ignored findSync()'s result and looped until the header
//      matched, which never happens for a sector that is not on the track:
//      findSync() seeks back to the track end, the position stops advancing,
//      and it spins forever. On the IEC task.
//   3. readSector() returned true when the block id was not $07, leaving the
//      PREVIOUS sector's bytes in the buffer to be served as this one's.
//
// The fixture is round-tripped through this project's own GCR decoder, so it
// proves the read path and the multi-block behaviour rather than the format
// itself. Every decoded block is compared against the .d64 it came from, which
// is an independent reference for the CONTENT even if not for the encoding.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/disk/g64.h"
#include "string_utils.h"

static const char* IMAGE = ".data/media/disk/g64/wolf64.g64";
static const char* SOURCE = ".data/media/disk/d64/wolf64.d64";

static const uint8_t SECTORS_PER_TRACK[36] = {
    0,
    21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
    19,19,19,19,19,19,19,
    18,18,18,18,18,18,
    17,17,17,17,17
};

class TestG64Stream : public G64MStream
{
public:
    using G64MStream::G64MStream;
    using G64MStream::sector_buffer;
};

static std::shared_ptr<TestG64Stream> openImage(std::shared_ptr<FileContainerStream>& src)
{
    src = std::make_shared<FileContainerStream>(IMAGE);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestG64Stream>(src);
    // mode is uninitialised on a directly constructed stream - only
    // MFile::getSourceStream() sets it.
    image->mode = std::ios_base::in;
    return image;
}

// The same block straight out of the .d64 the fixture was built from, which is
// the reference every decoded sector is checked against.
static bool referenceBlock(uint8_t track, uint8_t sector, uint8_t* out)
{
    FILE* fp = fopen(SOURCE, "rb");
    if (fp == nullptr)
        return false;

    uint32_t offset = 0;
    for (uint8_t t = 1; t < track; t++)
        offset += SECTORS_PER_TRACK[t] * 256;
    offset += sector * 256;

    bool ok = (fseek(fp, (long)offset, SEEK_SET) == 0) && (fread(out, 1, 256, fp) == 256);
    fclose(fp);
    return ok;
}

void setUp(void) {}
void tearDown(void) {}


// The disk header, which is the cheapest proof that the track table, the sync
// scan and the GCR nibble decode all work together.
void test_disk_header_decodes(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g64_read/host/make_g64.py");

    TEST_ASSERT_TRUE(image->seekSector(18, 0, 0));

    uint8_t block[256];
    TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));

    uint8_t expected[256];
    TEST_ASSERT_TRUE(referenceBlock(18, 0, expected));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, block, sizeof(block));
}

// Defect 1. Reading a whole block and then reading again is exactly what
// D64MStream::readFile() does between seeks, and it is what used to walk off
// the end of sector_buffer.
void test_reads_never_run_past_the_sector_buffer(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g64_read/host/make_g64.py");

    TEST_ASSERT_TRUE(image->seekSector(18, 1, 0));

    uint8_t whole[256];
    TEST_ASSERT_EQUAL_UINT32(sizeof(whole), image->readContainer(whole, sizeof(whole)));

    // The block is exhausted: further reads yield nothing, rather than whatever
    // follows the buffer in memory.
    uint8_t extra[64];
    TEST_ASSERT_EQUAL_UINT32(0, image->readContainer(extra, sizeof(extra)));

    // An oversized read from part-way in is clamped to what is left of the block.
    TEST_ASSERT_TRUE(image->seekSector(18, 1, 250));
    TEST_ASSERT_EQUAL_UINT32(6, image->readContainer(extra, sizeof(extra)));
    TEST_ASSERT_EQUAL_UINT32(0, image->readContainer(extra, sizeof(extra)));
}

// Defect 2. A sector that is not on the track has to fail, and fail promptly.
// Before the guard this spun forever, and the test hanging IS the failure mode.
void test_missing_sector_fails_instead_of_hanging(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g64_read/host/make_g64.py");

    // Track 35 holds 17 sectors, 0-16, so sector 17 is nowhere on it and the
    // scan has to go all the way round and give up.
    TEST_ASSERT_FALSE(image->seekSector(35, 17, 0));

    // The stream is still usable afterwards.
    TEST_ASSERT_TRUE(image->seekSector(35, 0, 0));
}

// Following a file's block chain the way readFile() does: read a block, then
// seek to the block its link names. This is the multi-block path defect 1 broke.
void test_file_chain_walks_and_matches_the_source(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g64_read/host/make_g64.py");

    // The first MULTI-block file in the directory - a one-block file would not
    // exercise the path that broke, since the bug only bit from the second
    // block on.
    uint8_t track = 0, sector = 0;
    uint16_t blocks = 0;

    uint8_t dir_track = 18, dir_sector = 1;
    for (int guard = 0; guard < 32 && dir_track != 0 && blocks < 2; guard++)
    {
        TEST_ASSERT_TRUE(image->seekSector(dir_track, dir_sector, 0));
        uint8_t dir[256];
        TEST_ASSERT_EQUAL_UINT32(sizeof(dir), image->readContainer(dir, sizeof(dir)));

        for (int i = 0; i < 8 && blocks < 2; i++)
        {
            const uint8_t* entry = dir + 2 + (i * 32);
            uint8_t kind = (uint8_t)(entry[0] & 0x0f);
            if ((entry[0] & 0x80) == 0 || kind < 1 || kind > 3)
                continue;

            uint16_t count = (uint16_t)(entry[28] | (entry[29] << 8));
            if (count < 2 || entry[1] < 1 || entry[1] > 35)
                continue;

            track = entry[1];
            sector = entry[2];
            blocks = count;
        }

        dir_track = dir[0];
        dir_sector = dir[1];
    }

    TEST_ASSERT_TRUE_MESSAGE(blocks > 1, "no multi-block file in the directory");

    uint16_t counted = 0;
    while (track != 0 && counted <= blocks)
    {
        TEST_ASSERT_TRUE(image->seekSector(track, sector, 0));

        uint8_t block[256];
        TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));

        uint8_t expected[256];
        TEST_ASSERT_TRUE(referenceBlock(track, sector, expected));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, block, sizeof(block));

        counted++;
        track = block[0];
        sector = block[1];
    }

    TEST_ASSERT_EQUAL_UINT8(0, track);
    TEST_ASSERT_EQUAL_UINT16(blocks, counted);
}

// The whole disk, every sector of every track, byte for byte against the .d64
// the fixture was made from. This is what would catch a sector served from the
// wrong place, a stale buffer (defect 3), or an off-by-one in the track table.
void test_every_sector_matches_the_source_image(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g64_read/host/make_g64.py");

    for (uint8_t track = 1; track <= 35; track++)
    {
        for (uint8_t sector = 0; sector < SECTORS_PER_TRACK[track]; sector++)
        {
            char message[48];
            snprintf(message, sizeof(message), "track %u sector %u",
                     (unsigned)track, (unsigned)sector);

            TEST_ASSERT_TRUE_MESSAGE(image->seekSector(track, sector, 0), message);

            uint8_t block[256];
            TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));

            uint8_t expected[256];
            TEST_ASSERT_TRUE(referenceBlock(track, sector, expected));
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, block, sizeof(block), message);
        }
    }
}

// Reading a file through the STREAM, which is the only test here that actually
// reproduces defect 1.
//
// The other tests call readContainer() straight after a seekSector(), so
// _position never advances and the old code - which indexed sector_buffer by
// _position - would pass them all. The bug needed the real path:
// MMediaStream::read() -> D64MStream::readFile() -> readContainer(), where
// _position grows with every byte returned, so from the second block on it
// indexed past the end of a 260-byte buffer and handed back adjacent memory.
//
// The expected content is walked out of the source .d64 independently.
void test_reading_a_file_through_the_stream_matches_the_source(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g64_read/host/make_g64.py");

    // Find the largest file in the directory, so the read crosses as many
    // blocks - and block boundaries - as this disk allows.
    char name[17] = { 0 };
    uint8_t start_track = 0, start_sector = 0;
    uint16_t blocks = 0;

    uint8_t dir_track = 18, dir_sector = 1;
    for (int guard = 0; guard < 32 && dir_track != 0; guard++)
    {
        TEST_ASSERT_TRUE(image->seekSector(dir_track, dir_sector, 0));
        uint8_t dir[256];
        TEST_ASSERT_EQUAL_UINT32(sizeof(dir), image->readContainer(dir, sizeof(dir)));

        for (int i = 0; i < 8; i++)
        {
            const uint8_t* entry = dir + 2 + (i * 32);
            uint8_t kind = (uint8_t)(entry[0] & 0x0f);
            if ((entry[0] & 0x80) == 0 || kind < 1 || kind > 3)
                continue;

            uint16_t count = (uint16_t)(entry[28] | (entry[29] << 8));
            if (count <= blocks || entry[1] < 1 || entry[1] > 35)
                continue;

            blocks = count;
            start_track = entry[1];
            start_sector = entry[2];
            std::memcpy(name, entry + 3, 16);
            for (int c = 15; c >= 0 && (uint8_t)name[c] == 0xa0; c--)
                name[c] = 0;
        }

        dir_track = dir[0];
        dir_sector = dir[1];
    }

    TEST_ASSERT_TRUE_MESSAGE(blocks > 2, "no file long enough to cross blocks");

    // Walk the chain in the SOURCE image to build what the file should contain.
    std::vector<uint8_t> expected;
    {
        uint8_t track = start_track, sector = start_sector;
        for (uint16_t guard = 0; guard <= blocks && track != 0; guard++)
        {
            uint8_t block[256];
            TEST_ASSERT_TRUE(referenceBlock(track, sector, block));

            if (block[0] == 0)
            {
                // Last block: the sector byte is the index of the last byte used.
                uint32_t used = (block[1] > 1) ? (uint32_t)(block[1] - 1) : 0;
                expected.insert(expected.end(), block + 2, block + 2 + used);
                break;
            }

            expected.insert(expected.end(), block + 2, block + 256);
            track = block[0];
            sector = block[1];
        }
    }
    TEST_ASSERT_TRUE(expected.size() > 512);

    // Now read the same file through the stream.
    // seekPath() matches against mstr::toUTF8() of the entry name, which is
    // how the drive asks - PETSCII $41-$5A map to lowercase a-z, so a bare
    // ASCII literal from the directory bytes matches nothing.
    std::string pattern = mstr::toUTF8(std::string(name));
    TEST_ASSERT_TRUE_MESSAGE(image->seekPath(pattern), pattern.c_str());

    std::vector<uint8_t> got;
    uint8_t buffer[256];
    for (int guard = 0; guard < 4096; guard++)
    {
        uint32_t n = image->read(buffer, sizeof(buffer));
        if (n == 0)
            break;
        got.insert(got.end(), buffer, buffer + n);
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)expected.size(), (uint32_t)got.size(), name);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected.data(), got.data(), expected.size(), name);
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_disk_header_decodes);
    RUN_TEST(test_reads_never_run_past_the_sector_buffer);
    RUN_TEST(test_missing_sector_fails_instead_of_hanging);
    RUN_TEST(test_file_chain_walks_and_matches_the_source);
    RUN_TEST(test_reading_a_file_through_the_stream_matches_the_source);
    RUN_TEST(test_every_sector_matches_the_source_image);

    return UNITY_END();
}
