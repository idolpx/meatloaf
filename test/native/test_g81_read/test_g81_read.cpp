// Read-path tests for the G81 MFM bitstream format.
//
// ** WHAT THESE DO AND DO NOT PROVE. **
//
// No .g81 exists in .data/media, VICE has no MFM-1581 support, and the P64
// reference implementation does not know the format. The entire specification
// is the four-line note at the top of g81.h, and the fixture generator
// (host/make_g81.py) encodes the SAME reading of that note that g81.cpp
// decodes. So these tests prove the two agree; they cannot prove either agrees
// with a real .g81. The container layout - where track data starts, and the
// four-byte length prefix - is the unverified part.
//
// What IS independently verified is the MFM layer underneath: findSync, the
// clock/data de-interleave, the address marks and CRC-16 all live in mfm.h and
// are exercised by test_p81_read against a REAL 1581 flux image. This suite
// reuses that layer rather than a copy of it, so a defect there fails in both
// places.
//
// One thing the fixture does check about the spec on its own: the length field
// is in BITS. The generator emits ~101000 for a track, which is the right order
// of magnitude for a 100000-cell 1581 rotation. Read as bytes it would be
// ~12500, the buffer would hold an eighth of a track, and the sector scan would
// find almost nothing - so the round trip working at all is weak evidence for
// that bullet, unlike the other two.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/disk/g81.h"

static const char* IMAGE = ".data/media/disk/g81/synth.g81";
static const char* SOURCE = ".data/media/disk/g81/synth.d81";

class TestG81Stream : public G81MStream
{
public:
    using G81MStream::G81MStream;
    using G81MStream::last_data_checksum_ok;
    using G81MStream::loadTrack;
    using G81MStream::mfm_track_bytes;
    using G81MStream::parseHeader;
    using G81MStream::physical_sector;
    using G81MStream::sector_buffer;
};

static std::shared_ptr<TestG81Stream> openImage(std::shared_ptr<FileContainerStream>& src)
{
    src = std::make_shared<FileContainerStream>(IMAGE);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestG81Stream>(src);
    // mode is uninitialised on a directly constructed stream - only
    // MFile::getSourceStream() sets it.
    image->mode = std::ios_base::in;
    return image;
}

// The same block straight out of the .d81 the fixture was built from.
static bool referenceBlock(uint8_t track, uint8_t sector, uint8_t* out)
{
    FILE* fp = fopen(SOURCE, "rb");
    if (fp == nullptr)
        return false;

    uint32_t offset = (((uint32_t)(track - 1) * 40) + sector) * 256;
    bool ok = (fseek(fp, (long)offset, SEEK_SET) == 0) && (fread(out, 1, 256, fp) == 256);
    fclose(fp);
    return ok;
}

void setUp(void) {}
void tearDown(void) {}


// The signature has to be enforced - the container looks enough like a .g64
// that a mislabelled file would otherwise be walked as one.
void test_header_and_signature(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g81_read/host/make_g81.py");

    TEST_ASSERT_TRUE(image->parseHeader());

    const std::string path = "build_g81_wrong_sig.bin";
    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    uint8_t header[64] = { 0 };
    std::memcpy(header, "GCR-1541", 8);
    fwrite(header, 1, sizeof(header), fp);
    fclose(fp);

    auto wrong_src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(wrong_src->isOpen());
    auto wrong = std::make_shared<TestG81Stream>(wrong_src);
    wrong->mode = std::ios_base::in;

    TEST_ASSERT_FALSE(wrong->parseHeader());
    TEST_ASSERT_FALSE(wrong->readHeader());

    wrong_src->close();
    remove(path.c_str());
}

// The per-track length is in BITS, and this is the one spec bullet the fixture
// can speak to on its own: a 1581 rotation is 100000 cells, so a track has to
// come out near 12500 bytes. Read as bytes instead, the buffer would be an
// eighth of that.
void test_track_length_is_in_bits(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g81_read/host/make_g81.py");

    TEST_ASSERT_TRUE(image->parseHeader());
    TEST_ASSERT_TRUE(image->loadTrack(0, 0));

    TEST_ASSERT_TRUE(image->mfm_track_bytes > 10000);
    TEST_ASSERT_TRUE(image->mfm_track_bytes <= G81_MAX_TRACK_BYTES);
}

// The disk header at CBM track 40 sector 0, which is what a listing reads.
void test_disk_header_decodes(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g81_read/host/make_g81.py");

    TEST_ASSERT_TRUE(image->readHeader());

    static const uint8_t expected_name[16] = {
        'G','8','1',' ','T','E','S','T',' ','D','I','S','K',' ',' ',' '
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_name, (const uint8_t*)image->header.name, 16);
    TEST_ASSERT_EQUAL_UINT8('8', (uint8_t)image->header.id_dos[0]);
    TEST_ASSERT_EQUAL_UINT8('1', (uint8_t)image->header.id_dos[1]);
    TEST_ASSERT_EQUAL_UINT8('3', (uint8_t)image->header.id_dos[3]);
    TEST_ASSERT_EQUAL_UINT8('D', (uint8_t)image->header.id_dos[4]);
}

// Two logical blocks share one physical 512-byte sector, and the halving has to
// put them the right way round.
void test_two_blocks_share_one_physical_sector(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g81_read/host/make_g81.py");

    TEST_ASSERT_TRUE(image->parseHeader());

    TEST_ASSERT_TRUE(image->seekSector(20, 4, 0));
    uint8_t low[256];
    TEST_ASSERT_EQUAL_UINT32(sizeof(low), image->readContainer(low, sizeof(low)));

    TEST_ASSERT_TRUE(image->seekSector(20, 5, 0));
    uint8_t high[256];
    TEST_ASSERT_EQUAL_UINT32(sizeof(high), image->readContainer(high, sizeof(high)));

    TEST_ASSERT_EQUAL_UINT8_ARRAY(image->physical_sector, low, 256);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(image->physical_sector + 256, high, 256);
    TEST_ASSERT_TRUE(std::memcmp(low, high, 256) != 0);
}

// seekSector() plus readContainer(), including the offset landing inside the
// block rather than the file, and the bounds at both ends of the geometry.
void test_seek_sector_positions_read_container(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g81_read/host/make_g81.py");

    TEST_ASSERT_TRUE(image->parseHeader());

    TEST_ASSERT_TRUE(image->seekSector(40, 0, 0));
    uint8_t link[2] = { 0, 0 };
    TEST_ASSERT_EQUAL_UINT32(2, image->readContainer(link, 2));
    TEST_ASSERT_EQUAL_UINT8(40, link[0]);
    TEST_ASSERT_EQUAL_UINT8(3, link[1]);

    TEST_ASSERT_TRUE(image->seekSector(40, 0, 25));
    uint8_t marker[2] = { 0, 0 };
    TEST_ASSERT_EQUAL_UINT32(2, image->readContainer(marker, 2));
    TEST_ASSERT_EQUAL_UINT8('3', marker[0]);
    TEST_ASSERT_EQUAL_UINT8('D', marker[1]);

    TEST_ASSERT_TRUE(image->seekSector(40, 0, 250));
    uint8_t tail[64];
    TEST_ASSERT_EQUAL_UINT32(6, image->readContainer(tail, sizeof(tail)));
    TEST_ASSERT_EQUAL_UINT32(0, image->readContainer(tail, sizeof(tail)));

    TEST_ASSERT_FALSE(image->seekSector(0, 0, 0));
    TEST_ASSERT_FALSE(image->seekSector(81, 0, 0));
    TEST_ASSERT_FALSE(image->seekSector(40, 40, 0));
}

// Reading a file through the STREAM - read() -> readFile() -> readContainer() -
// which is the path where _position advances and a sector-buffer cursor bug
// would show up. The same class of defect that was live in g64.cpp.
void test_reading_a_file_through_the_stream(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g81_read/host/make_g81.py");

    TEST_ASSERT_TRUE(image->parseHeader());
    TEST_ASSERT_TRUE(image->seekPath("hello"));

    std::vector<uint8_t> got;
    uint8_t buffer[128];
    for (int guard = 0; guard < 128; guard++)
    {
        uint32_t n = image->read(buffer, sizeof(buffer));
        if (n == 0)
            break;
        got.insert(got.end(), buffer, buffer + n);
    }

    // Three blocks: two full at 254 data bytes, then 100 used in the last.
    TEST_ASSERT_EQUAL_UINT32(254 + 254 + 100, (uint32_t)got.size());

    uint8_t block[256];
    TEST_ASSERT_TRUE(referenceBlock(1, 0, block));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(block + 2, got.data(), 254);
    TEST_ASSERT_TRUE(referenceBlock(1, 1, block));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(block + 2, got.data() + 254, 254);
}

// Writes must never reach the container.
void test_writes_are_refused(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g81_read/host/make_g81.py");

    uint8_t data[16] = { 0 };
    TEST_ASSERT_EQUAL_UINT32(0, image->writeContainer(data, sizeof(data)));
}

// Every block of every track on both heads, byte for byte against the .d81.
// 3200 blocks, each carrying a pattern derived from its own track and sector,
// so a block served from the wrong cylinder or the wrong head is caught by
// content rather than by a CRC that would pass either way.
void test_every_block_matches_the_source_image(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("fixture missing - run test/native/test_g81_read/host/make_g81.py");

    TEST_ASSERT_TRUE(image->parseHeader());

    for (uint8_t track = 1; track <= 80; track++)
    {
        for (uint8_t sector = 0; sector < 40; sector++)
        {
            char message[48];
            snprintf(message, sizeof(message), "track %u sector %u",
                     (unsigned)track, (unsigned)sector);

            TEST_ASSERT_TRUE_MESSAGE(image->seekSector(track, sector, 0), message);
            TEST_ASSERT_TRUE_MESSAGE(image->last_data_checksum_ok, message);

            uint8_t block[256];
            TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));

            uint8_t expected[256];
            TEST_ASSERT_TRUE(referenceBlock(track, sector, expected));
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, block, sizeof(block), message);
        }
    }
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_header_and_signature);
    RUN_TEST(test_track_length_is_in_bits);
    RUN_TEST(test_disk_header_decodes);
    RUN_TEST(test_two_blocks_share_one_physical_sector);
    RUN_TEST(test_seek_sector_positions_read_container);
    RUN_TEST(test_reading_a_file_through_the_stream);
    RUN_TEST(test_writes_are_refused);
    RUN_TEST(test_every_block_matches_the_source_image);

    return UNITY_END();
}
