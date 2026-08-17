// Read-path tests for the G71 GCR bitstream format.
//
// A .g71 is a .g64 with a different signature and a 1571's geometry - same
// container, same GCR, same read logic, because a 1571 in double-sided mode is
// two 1541 surfaces written by the same hardware at the same four speed zones.
// So what these tests have to prove is narrow and specific:
//
//   1. The signature is enforced, and a .g64 is not silently read as a .g71.
//   2. Track numbering is FLAT - track N is at half track N * 2 for all 70
//      tracks, with no per-side base - so side 2 (tracks 36-70) lands where
//      G64MStream::seekSector() already looks. This is the one thing that could
//      plausibly have been laid out another way.
//   3. The speed zones repeat on side 2, so tracks 36-70 have the right sector
//      counts. Get this wrong and side 2 either loses sectors or gains ones
//      that are not there.
//
// The fixture is generated from a synthesized .d71 by
// test/native/test_g64_read/host/make_g64.py, since no .g71 exists in .archive
// and the firmware has a GCR decoder but no encoder. Every block of that .d71
// carries a pattern derived from its own track and sector number, so a block
// served from the wrong place is caught by its CONTENT rather than by a
// checksum that would pass either way.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/disk/g71.h"

static const char* IMAGE = ".archive/disk/g71/synth.g71";
static const char* SOURCE = ".archive/disk/g71/synth.d71";
static const char* G64_IMAGE = ".archive/disk/g64/wolf64.g64";

// 70 tracks: the 1541 progression, twice.
static uint8_t sectorsOnTrack(uint8_t track)
{
    uint8_t t = (track <= 35) ? track : (uint8_t)(track - 35);
    if (t < 18) return 21;
    if (t < 25) return 19;
    if (t < 31) return 18;
    return 17;
}

static std::shared_ptr<G71MStream> openImage(std::shared_ptr<FileContainerStream>& src,
                                             const char* path = IMAGE)
{
    src = std::make_shared<FileContainerStream>(path);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<G71MStream>(src);
    // mode is uninitialised on a directly constructed stream - only
    // MFile::getSourceStream() sets it.
    image->mode = std::ios_base::in;
    return image;
}

// The same block straight out of the .d71 the fixture was built from.
static bool referenceBlock(uint8_t track, uint8_t sector, uint8_t* out)
{
    FILE* fp = fopen(SOURCE, "rb");
    if (fp == nullptr)
        return false;

    uint32_t offset = 0;
    for (uint8_t t = 1; t < track; t++)
        offset += sectorsOnTrack(t) * 256;
    offset += sector * 256;

    bool ok = (fseek(fp, (long)offset, SEEK_SET) == 0) && (fread(out, 1, 256, fp) == 256);
    fclose(fp);
    return ok;
}

void setUp(void) {}
void tearDown(void) {}


// The signature has to be enforced, or a .g64 opened as a .g71 would read 35
// tracks of real data and then fail confusingly on the 36th.
void test_signature_is_enforced(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g64_read/host/make_g64.py");

    TEST_ASSERT_TRUE(image->readHeader());

    std::shared_ptr<FileContainerStream> g64src;
    auto wrong = openImage(g64src, G64_IMAGE);
    if (wrong != nullptr)
        TEST_ASSERT_FALSE_MESSAGE(wrong->readHeader(), "a GCR-1541 image was accepted as a .g71");
}

// Side 1, which a .g64 would also have. Proves nothing about the 1571 layout on
// its own, but it separates "the container is broken" from "side 2 is broken"
// when something fails below.
void test_side_one_reads(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g64_read/host/make_g64.py");

    for (uint8_t track = 1; track <= 35; track++)
    {
        uint8_t block[256], expected[256];
        char message[48];
        snprintf(message, sizeof(message), "track %u sector 0", (unsigned)track);

        TEST_ASSERT_TRUE_MESSAGE(image->seekSector(track, 0, 0), message);
        TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));
        TEST_ASSERT_TRUE(referenceBlock(track, 0, expected));
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, block, sizeof(block), message);
    }
}

// Side 2 - tracks 36-70 - which is the whole point of the format and the only
// part whose addressing could have been laid out differently. Every block
// carries a pattern derived from its own track and sector, so a block fetched
// from the wrong half track is caught by content.
void test_side_two_is_addressed_flat(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g64_read/host/make_g64.py");

    for (uint8_t track = 36; track <= 70; track++)
    {
        uint8_t block[256], expected[256];
        char message[48];
        snprintf(message, sizeof(message), "track %u sector 0", (unsigned)track);

        TEST_ASSERT_TRUE_MESSAGE(image->seekSector(track, 0, 0), message);
        TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));
        TEST_ASSERT_TRUE(referenceBlock(track, 0, expected));
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, block, sizeof(block), message);
    }
}

// The speed zones repeat on side 2, so track 36 holds 21 sectors and track 70
// holds 17. A wrong zone map shows up as a sector that should exist being
// rejected, or one that should not being accepted.
void test_side_two_sector_counts(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g64_read/host/make_g64.py");

    TEST_ASSERT_EQUAL_UINT16(21, image->getSectorCount(36));
    TEST_ASSERT_EQUAL_UINT16(19, image->getSectorCount(53));
    TEST_ASSERT_EQUAL_UINT16(18, image->getSectorCount(60));
    TEST_ASSERT_EQUAL_UINT16(17, image->getSectorCount(70));

    // The last sector of the last track exists; one past it does not.
    TEST_ASSERT_TRUE(image->seekSector(70, 16, 0));
    TEST_ASSERT_FALSE(image->seekSector(70, 17, 0));

    // And a track past the end of the disk is refused.
    TEST_ASSERT_FALSE(image->seekSector(71, 0, 0));
}

// Every sector of all 70 tracks, byte for byte. This is what catches an
// off-by-one in the track table, a sector served from a neighbour, or a stale
// buffer.
void test_every_sector_matches_the_source_image(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g64_read/host/make_g64.py");

    for (uint8_t track = 1; track <= 70; track++)
    {
        for (uint8_t sector = 0; sector < sectorsOnTrack(track); sector++)
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

// Writes must never reach the container: D64MStream's write path addresses it
// as a linear .d64, which on a GCR bitstream lands in the middle of encoded
// data. MFile::isWritable inherits true from the SD card, so this guard is the
// only thing in the way.
void test_writes_are_refused(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g64_read/host/make_g64.py");

    uint8_t data[16] = { 0 };
    TEST_ASSERT_EQUAL_UINT32(0, image->writeContainer(data, sizeof(data)));
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_signature_is_enforced);
    RUN_TEST(test_side_one_reads);
    RUN_TEST(test_side_two_is_addressed_flat);
    RUN_TEST(test_side_two_sector_counts);
    RUN_TEST(test_writes_are_refused);
    RUN_TEST(test_every_sector_matches_the_source_image);

    return UNITY_END();
}
