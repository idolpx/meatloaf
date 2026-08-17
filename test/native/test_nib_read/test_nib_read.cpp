// Read-path tests for the NIB raw-GCR nibbler format.
//
// nib.cpp was a near-copy of the old g64.cpp and carried the same defects, plus
// two of its own. What is pinned here:
//
//   1. readContainer() indexed sector_buffer by _position - the position in the
//      FILE - so every block of a file after the first read past the end of the
//      buffer and returned adjacent memory as file content.
//   2. The track-table search was `do { read 2 bytes } while (wanted != found
//      && found != 0)`, unbounded: a track not in the table walked off the end
//      of the header and through the track data with nothing to stop it.
//   3. The sector search called readSectorHeader() BEFORE finding any sync, and
//      looped on a flag a failing findSync() could leave set.
//   4. readSector() returned true when the data block id was not $07, leaving
//      the previous sector's bytes to be served as this one's.
//   5. The header checksum was computed and thrown away - it existed only to be
//      printf'd, and those printf()s ran on the IEC task on every sector read.
//
// It also read ONE BYTE AT A TIME straight from the container while scanning
// for syncs, which over a network is thousands of range requests per sector.
// The track is now pulled into RAM once and scanned there.
//
// Fixtures are generated from a real .d64 by host/make_nib.py, since no .nib
// exists in .archive. Every decoded block is compared against that .d64, which
// is an independent reference for the CONTENT even though the GCR encoding is
// this project's own round trip.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/disk/nib.h"
#include "string_utils.h"

static const char* NIB_IMAGE = ".archive/disk/nib/wolf64.nib";
static const char* NB2_IMAGE = ".archive/disk/nib/wolf64.nb2";
static const char* NBZ_IMAGE = ".archive/disk/nib/wolf64.nbz";
static const char* SOURCE = ".archive/disk/d64/wolf64.d64";

static const uint8_t SECTORS_PER_TRACK[36] = {
    0,
    21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
    19,19,19,19,19,19,19,
    18,18,18,18,18,18,
    17,17,17,17,17
};

class TestNIBStream : public NIBMStream
{
public:
    using NIBMStream::NIBMStream;
    using NIBMStream::last_data_checksum_ok;
    using NIBMStream::parseHeader;
    using NIBMStream::sector_buffer;
    using NIBMStream::track_stride;
    using NIBMStream::inflated;
    using NIBMStream::base_offset;
};

static std::shared_ptr<TestNIBStream> openImage(std::shared_ptr<FileContainerStream>& src,
                                                const char* path = NIB_IMAGE)
{
    src = std::make_shared<FileContainerStream>(path);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestNIBStream>(src);
    // mode is uninitialised on a directly constructed stream - only
    // MFile::getSourceStream() sets it.
    image->mode = std::ios_base::in;
    return image;
}

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


// The signature is checked, and the header is read from offset 0 rather than
// from wherever the stream was left after delegating to the D64 layer.
void test_header_and_signature(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_nib_read/host/make_nib.py");

    TEST_ASSERT_TRUE(image->parseHeader());
    TEST_ASSERT_EQUAL_UINT32(NIB_TRACK_LENGTH, image->track_stride);

    const std::string path = "build_nib_wrong_sig.bin";
    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    uint8_t header[512] = { 0 };
    std::memcpy(header, "GCR-1541", 8);
    fwrite(header, 1, sizeof(header), fp);
    fclose(fp);

    auto wrong_src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(wrong_src->isOpen());
    auto wrong = std::make_shared<TestNIBStream>(wrong_src);
    wrong->mode = std::ios_base::in;

    TEST_ASSERT_FALSE(wrong->parseHeader());
    TEST_ASSERT_FALSE(wrong->readHeader());

    wrong_src->close();
    remove(path.c_str());
}

// The disk header, which is the cheapest proof that the track table, the sync
// scan and the GCR decode agree.
void test_disk_header_decodes(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_nib_read/host/make_nib.py");

    TEST_ASSERT_TRUE(image->seekSector(18, 0, 0));

    uint8_t block[256], expected[256];
    TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));
    TEST_ASSERT_TRUE(referenceBlock(18, 0, expected));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, block, sizeof(block));
}

// Defect 1. Reading a whole block and then reading again is what readFile()
// does between seeks, and it is what used to walk off the end of the buffer.
void test_reads_never_run_past_the_sector_buffer(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_nib_read/host/make_nib.py");

    TEST_ASSERT_TRUE(image->seekSector(18, 1, 0));

    uint8_t whole[256];
    TEST_ASSERT_EQUAL_UINT32(sizeof(whole), image->readContainer(whole, sizeof(whole)));

    uint8_t extra[64];
    TEST_ASSERT_EQUAL_UINT32(0, image->readContainer(extra, sizeof(extra)));

    TEST_ASSERT_TRUE(image->seekSector(18, 1, 250));
    TEST_ASSERT_EQUAL_UINT32(6, image->readContainer(extra, sizeof(extra)));
    TEST_ASSERT_EQUAL_UINT32(0, image->readContainer(extra, sizeof(extra)));
}

// Defects 2 and 3. A sector that is not on the track, and a track the image
// does not carry, both have to fail promptly rather than run off the end of
// something. The test hanging IS the old failure mode.
void test_missing_track_and_sector_fail(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_nib_read/host/make_nib.py");

    TEST_ASSERT_TRUE(image->parseHeader());

    // Track 35 holds 17 sectors, 0-16.
    TEST_ASSERT_FALSE(image->seekSector(35, 17, 0));
    TEST_ASSERT_FALSE(image->seekSector(0, 0, 0));
    TEST_ASSERT_FALSE(image->seekSector(36, 0, 0));

    // Still usable afterwards.
    TEST_ASSERT_TRUE(image->seekSector(35, 0, 0));
}

// Writes must never reach the container.
void test_writes_are_refused(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_nib_read/host/make_nib.py");

    uint8_t data[16] = { 0 };
    TEST_ASSERT_EQUAL_UINT32(0, image->writeContainer(data, sizeof(data)));
}

// Reading a file through the STREAM - read() -> readFile() -> readContainer() -
// which is the only path where _position advances, and therefore the only one
// that reproduces defect 1. Every other test here would pass with the old code.
void test_reading_a_file_through_the_stream(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_nib_read/host/make_nib.py");

    // The largest file in the directory, so the read crosses as many blocks as
    // this disk allows.
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

    // Walk the chain in the SOURCE image to build what the file should hold.
    std::vector<uint8_t> expected;
    {
        uint8_t track = start_track, sector = start_sector;
        for (uint16_t guard = 0; guard <= blocks && track != 0; guard++)
        {
            uint8_t block[256];
            TEST_ASSERT_TRUE(referenceBlock(track, sector, block));

            if (block[0] == 0)
            {
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

    // seekPath() matches against mstr::toUTF8() of the entry name - PETSCII
    // $41-$5A map to lowercase - so the raw directory bytes match nothing.
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

    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)expected.size(), (uint32_t)got.size(), pattern.c_str());
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected.data(), got.data(), expected.size(), pattern.c_str());
}

// A .nb2 holds several passes of each track, and nothing in the header says how
// many - the stride is derived from the file length. So the same reader has to
// produce byte-identical results from a 1-pass .nib and a 4-pass .nb2 of the
// same disk.
void test_nb2_multi_pass_reads_identically(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src, NB2_IMAGE);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run make_nib.py -p 4");

    TEST_ASSERT_TRUE(image->parseHeader());
    TEST_ASSERT_EQUAL_UINT32(NIB_TRACK_LENGTH * 4, image->track_stride);

    for (uint8_t track = 1; track <= 35; track++)
    {
        for (uint8_t sector = 0; sector < SECTORS_PER_TRACK[track]; sector++)
        {
            char message[48];
            snprintf(message, sizeof(message), "track %u sector %u",
                     (unsigned)track, (unsigned)sector);

            TEST_ASSERT_TRUE_MESSAGE(image->seekSector(track, sector, 0), message);

            uint8_t block[256], expected[256];
            TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));
            TEST_ASSERT_TRUE(referenceBlock(track, sector, expected));
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, block, sizeof(block), message);
        }
    }
}

// Every sector of every track, byte for byte against the .d64 the fixture was
// made from.
void test_every_sector_matches_the_source_image(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_nib_read/host/make_nib.py");

    for (uint8_t track = 1; track <= 35; track++)
    {
        for (uint8_t sector = 0; sector < SECTORS_PER_TRACK[track]; sector++)
        {
            char message[48];
            snprintf(message, sizeof(message), "track %u sector %u",
                     (unsigned)track, (unsigned)sector);

            TEST_ASSERT_TRUE_MESSAGE(image->seekSector(track, sector, 0), message);
            TEST_ASSERT_TRUE_MESSAGE(image->last_data_checksum_ok, message);

            uint8_t block[256], expected[256];
            TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));
            TEST_ASSERT_TRUE(referenceBlock(track, sector, expected));
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, block, sizeof(block), message);
        }
    }
}


// A .nbz is a COMPRESSED .nib, and the container is identified by content
// rather than by extension - so a gzip magic number is inflated wholesale
// (gzip has no random access) and then read from memory. Every block has to
// come out identical to the uncompressed image.
void test_nbz_gzip_reads_identically(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src, NBZ_IMAGE);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - gzip the .nib to .nbz");

    TEST_ASSERT_TRUE(image->parseHeader());
    TEST_ASSERT_TRUE(image->inflated);
    TEST_ASSERT_EQUAL_UINT32(NIB_TRACK_LENGTH, image->track_stride);

    for (uint8_t track = 1; track <= 35; track++)
    {
        for (uint8_t sector = 0; sector < SECTORS_PER_TRACK[track]; sector++)
        {
            char message[48];
            snprintf(message, sizeof(message), "track %u sector %u",
                     (unsigned)track, (unsigned)sector);

            TEST_ASSERT_TRUE_MESSAGE(image->seekSector(track, sector, 0), message);

            uint8_t block[256], expected[256];
            TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));
            TEST_ASSERT_TRUE(referenceBlock(track, sector, expected));
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, block, sizeof(block), message);
        }
    }
}

// A real .nbz is refused, clearly, rather than half-read.
//
// This is what the corpus actually contains, and it is not what was first
// guessed at: the signature really does sit one byte in, as
// MFileSystem::byContent() always claimed, but behind a leading $05 and with a
// version byte of 3 - and the tracks behind the table are COMPRESSED to
// variable lengths (about 1766 bytes each against a plain track's 8192). The
// per-track compression is not implemented, so the container is recognised and
// rejected; the track table alone would be no use.
void test_real_nbz_is_recognised_and_refused(void)
{
    static const char* NBZ_CORPUS[] = {
        ".archive/disk/nbz/ghostbusters[activision_1984](aa)(ntsc)(!).nbz",
        ".archive/disk/nbz/altered_beast[sega_1987](ntsc)(!).nbz",
    };

    int checked = 0;
    for (size_t i = 0; i < sizeof(NBZ_CORPUS) / sizeof(NBZ_CORPUS[0]); i++)
    {
        std::shared_ptr<FileContainerStream> src;
        auto image = openImage(src, NBZ_CORPUS[i]);
        if (image == nullptr)
            continue;

        TEST_ASSERT_FALSE_MESSAGE(image->parseHeader(), NBZ_CORPUS[i]);
        TEST_ASSERT_FALSE_MESSAGE(image->readHeader(), NBZ_CORPUS[i]);
        checked++;
    }

    if (checked == 0)
        TEST_IGNORE_MESSAGE("no .nbz in .archive/disk/nbz");
}

// The real corpus: 41 nibbler dumps of commercial disks, none of them made by
// this project. There is no reference image to compare content against, so
// what is asserted is what a CBM disk cannot fake - the container parses, its
// stride comes out an exact number of track windows, and track 18 sector 0
// decodes as a disk header with the DOS version byte 'A'.
//
// These are protected originals, so tracks OUTSIDE the directory are expected
// to fail in all sorts of ways; that is the media, not the reader.
void test_real_nib_corpus_parses_and_reads_its_directory(void)
{
    static const char* REAL[] = {
        ".archive/disk/nib/bubble_bobble[firebird_1987]-1fb126.nib",
        ".archive/disk/nib/california_games_s1[epyx_1987]-2e35fe.nib",
        ".archive/disk/nib/impossible_mission[epyx_1984]-c00eb9.nib",
        ".archive/disk/nib/maniac_mansion_s1[lucasfilm_1989](ntsc)-fca0c7.nib",
        ".archive/disk/nib/vipterm.nib",
    };

    int checked = 0;
    int with_directory = 0;

    for (size_t i = 0; i < sizeof(REAL) / sizeof(REAL[0]); i++)
    {
        std::shared_ptr<FileContainerStream> src;
        auto image = openImage(src, REAL[i]);
        if (image == nullptr)
            continue;

        TEST_ASSERT_TRUE_MESSAGE(image->parseHeader(), REAL[i]);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(NIB_TRACK_LENGTH, image->track_stride, REAL[i]);
        checked++;

        // A protected disk can do anything it likes with its directory track,
        // so this counts rather than requires - but if NONE of them read, the
        // reader is broken rather than the media.
        if (image->seekSector(18, 0, 0) && image->sector_buffer[2] == 0x41)
            with_directory++;
    }

    if (checked == 0)
        TEST_IGNORE_MESSAGE("no real .nib images in .archive/disk/nib");

    printf("real nib corpus: %d parsed, %d with a readable CBM directory track\n",
           checked, with_directory);

    TEST_ASSERT_TRUE_MESSAGE(with_directory > 0,
        "no real image produced a CBM disk header - the reader, not the media");
}

// Something that is neither gzip nor a NIB has to be refused rather than
// inflated into nothing or walked as a track table.
void test_unrecognised_container_is_refused(void)
{
    const std::string path = "build_nib_not_a_nib.bin";
    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    uint8_t junk[512];
    for (size_t i = 0; i < sizeof(junk); i++)
        junk[i] = (uint8_t)(i * 7);
    fwrite(junk, 1, sizeof(junk), fp);
    fclose(fp);

    auto src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(src->isOpen());
    auto image = std::make_shared<TestNIBStream>(src);
    image->mode = std::ios_base::in;

    TEST_ASSERT_FALSE(image->parseHeader());
    TEST_ASSERT_FALSE(image->readHeader());

    src->close();
    remove(path.c_str());
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_header_and_signature);
    RUN_TEST(test_disk_header_decodes);
    RUN_TEST(test_reads_never_run_past_the_sector_buffer);
    RUN_TEST(test_missing_track_and_sector_fail);
    RUN_TEST(test_writes_are_refused);
    RUN_TEST(test_reading_a_file_through_the_stream);
    RUN_TEST(test_nb2_multi_pass_reads_identically);
    RUN_TEST(test_nbz_gzip_reads_identically);
    RUN_TEST(test_real_nbz_is_recognised_and_refused);
    RUN_TEST(test_real_nib_corpus_parses_and_reads_its_directory);
    RUN_TEST(test_unrecognised_container_is_refused);
    RUN_TEST(test_every_sector_matches_the_source_image);

    return UNITY_END();
}
