// Read-path tests for the SPYne container format.
//
// A SPYne stores its files uncompressed, so the thing that can go wrong is
// GEOMETRY: where the directory starts, how one directory entry's offset
// relates to the next, and where the file data begins. Every one of those is
// counted in 254-byte blocks, and getting any of them wrong yields entries
// that are plausible but a few bytes adrift - which is exactly the defect
// lnx.cpp shipped with when it used the inherited 256-byte block_size.
//
// The oracle is the checksum every directory entry carries: a 16-bit sum
// without carry over the file's DECOMPRESSED bytes, written by the archiver.
// It is computed over exactly the derived size, so an entry that starts two
// bytes late, or that is one byte too long, fails it. Nothing weaker can tell
// a right offset from a nearly-right one.
//
// The corpus is real - five .SPY files in .data/media/archive/spy, none of them
// written by this project. .data/media is gitignored, so these skip cleanly
// without it.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/archive/spy.h"
#include "string_utils.h"

static const char* CORPUS_DIR = ".data/media/archive/spy";

static const char* CORPUS[] = {
    "NTSC4K-2.SPY", "PARTY-97.SPY", "TURBO-MP.SPY", "WICKEDS1.SPY", "WICKEDS2.SPY",
};
static const size_t CORPUS_COUNT = sizeof(CORPUS) / sizeof(CORPUS[0]);

// The geometry constants the container is laid out with, restated here so the
// tests check the format rather than agreeing with spy.cpp's arithmetic.
static constexpr uint32_t SPY_BLOCK = 254;
static constexpr uint32_t SPY_DIR_START = 15 * SPY_BLOCK;   // 3810

class TestSPYStream : public SPYMStream
{
public:
    using SPYMStream::SPYMStream;
    using SPYMStream::decodeType;
    using SPYMStream::entries;
    using SPYMStream::entry;
    using SPYMStream::readHeader;
    using SPYMStream::seekEntry;
    using SPYMStream::seekPath;
};

static std::shared_ptr<FileContainerStream> g_src;

static std::shared_ptr<TestSPYStream> openImage(const std::string& path,
                                                std::shared_ptr<FileContainerStream>& src)
{
    src = std::make_shared<FileContainerStream>(path);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestSPYStream>(src);
    // mode is uninitialised on a directly constructed stream - only
    // MFile::getSourceStream() sets it, and garbage carrying the out bit sends
    // seekPath() down the write branch.
    image->mode = std::ios_base::in;
    return image;
}

// Reads one entry through the real stream path and returns its bytes.
static std::vector<uint8_t> readEntry(std::shared_ptr<TestSPYStream> image,
                                      const std::string& name)
{
    std::vector<uint8_t> got;
    if (!image->seekPath(name))
        return got;

    uint8_t buffer[256];
    for (int guard = 0; guard < 65536; guard++)
    {
        uint32_t n = image->read(buffer, sizeof(buffer));
        if (n == 0)
            break;
        got.insert(got.end(), buffer, buffer + n);
    }
    return got;
}

static uint16_t spySum(const std::vector<uint8_t>& bytes)
{
    uint16_t sum = 0;
    for (uint8_t b : bytes)
        sum = (uint16_t)(sum + b);
    return sum;
}

void setUp(void) {}

void tearDown(void)
{
    // Artifacts are removed here, never inline: on Windows remove() fails
    // while the stream still holds the file open, and a failed assertion skips
    // any cleanup below it.
    if (g_src != nullptr)
    {
        g_src->close();
        g_src = nullptr;
    }
    remove("build_spy_not_a_spy.bin");
}


// The directory walk, and the field ranges it depends on.
void test_directory_lists_entries(void)
{
    auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[0], g_src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/spy");

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(27, (uint32_t)image->entries.size());

    for (const auto& e : image->entries)
    {
        TEST_ASSERT_TRUE(e.file_type >= 0x81 && e.file_type <= 0x83);
        TEST_ASSERT_TRUE(e.blocks > 0);
        TEST_ASSERT_TRUE(e.lsu > 0);
        TEST_ASSERT_TRUE(e.filename.size() > 0 && e.filename.size() <= 16);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)(e.blocks - 1) * SPY_BLOCK + (e.lsu - 1), e.size);
    }
}

// Eight entries fit in one 254-byte directory block: seven of 32 bytes plus one
// of 30, because the eighth entry's two filler bytes would cross the boundary
// and so are not written. That makes the stride WITHIN a block and the stride
// BETWEEN blocks two different numbers, and a flat `i * 32` walks two bytes
// further adrift with every block.
//
// This pins it directly: the ninth entry's name has to be the one sitting at
// the second directory block's first byte.
void test_ninth_entry_starts_at_the_next_directory_block(void)
{
    auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[0], g_src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/spy");

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_TRUE(image->entries.size() > 8);

    // Read the name field of the entry the format says lives there, straight
    // out of the container, without going through the parser under test.
    uint8_t raw[16];
    TEST_ASSERT_TRUE(g_src->seek(SPY_DIR_START + SPY_BLOCK + 0x03));
    uint32_t got = 0;
    while (got < sizeof(raw))
    {
        uint32_t n = g_src->read(raw + got, sizeof(raw) - got);
        TEST_ASSERT_TRUE(n > 0);
        got += n;
    }

    std::string expected((const char*)raw, sizeof(raw));
    mstr::rtrimA0(expected);

    TEST_ASSERT_EQUAL_STRING(expected.c_str(), image->entries[8].filename.c_str());
}

// The files tile the container: each starts where the previous one's declared
// block count ends, and the last one ends at EOF - to within its own final
// block, in EITHER direction. Over, when the archiver did not store the
// padding after the last file's real bytes (PARTY-97, WICKEDS1, WICKEDS2);
// under, when the container carries a few bytes past it (NTSC4K-2 by 42,
// TURBO-MP by 20). Both are inside one 254-byte block, and that bound is what
// makes this a test: a block-size error puts the end out by two bytes per
// block, which across dozens of entries is thousands of bytes adrift.
//
// Deliberately NOT the exact-tiling assertion the LNX suite makes - that one
// fails here, and the difference is the padding, not the arithmetic.
void test_entries_tile_the_container(void)
{
    for (size_t c = 0; c < CORPUS_COUNT; c++)
    {
        auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[c], g_src);
        if (image == nullptr)
            TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/spy");

        TEST_ASSERT_TRUE(image->readHeader());
        TEST_ASSERT_TRUE(image->entries.size() > 0);

        const uint32_t dir_blocks = (uint32_t)((image->entries.size() + 7) / 8);
        uint32_t offset = (15 + dir_blocks) * SPY_BLOCK;

        for (const auto& e : image->entries)
        {
            char message[128];
            snprintf(message, sizeof(message), "%s entry [%s]", CORPUS[c], e.filename.c_str());
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(offset, e.offset, message);
            offset += (uint32_t)e.blocks * SPY_BLOCK;
        }

        const uint32_t file_size = g_src->size();
        const uint32_t drift = (offset > file_size) ? (offset - file_size)
                                                    : (file_size - offset);
        char message[160];
        snprintf(message, sizeof(message),
                 "%s: computed end %lu against file size %lu, drift %lu",
                 CORPUS[c], (unsigned long)offset, (unsigned long)file_size,
                 (unsigned long)drift);
        TEST_ASSERT_TRUE_MESSAGE(drift < SPY_BLOCK, message);

        g_src->close();
        g_src = nullptr;
    }
}

// Entries are found by name the way the drive asks for them - through
// mstr::toUTF8() of the stored name, since PETSCII $41-$5A map to lowercase.
void test_entries_are_found_by_name(void)
{
    auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[0], g_src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/spy");

    TEST_ASSERT_TRUE(image->readHeader());

    std::string wanted = mstr::toUTF8(image->entries[3].filename);
    TEST_ASSERT_TRUE_MESSAGE(image->seekEntry(wanted), wanted.c_str());
    TEST_ASSERT_EQUAL_STRING(image->entries[3].filename.c_str(), image->entry.filename.c_str());

    // A wildcard resolves to the first match, as it does everywhere else.
    TEST_ASSERT_TRUE(image->seekEntry(std::string("*")));

    TEST_ASSERT_FALSE(image->seekEntry(std::string("no such file here")));
}

// The whole point. Every entry of every container in the corpus is read through
// the stream and checked against the checksum its directory entry carries.
void test_every_entry_matches_its_stored_checksum(void)
{
    size_t checked = 0;

    for (size_t c = 0; c < CORPUS_COUNT; c++)
    {
        auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[c], g_src);
        if (image == nullptr)
            TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/spy");

        TEST_ASSERT_TRUE(image->readHeader());

        for (size_t i = 0; i < image->entries.size(); i++)
        {
            // Captured before seekPath(), which overwrites `entry`.
            const std::string stored = image->entries[i].filename;
            const uint32_t expected_size = image->entries[i].size;
            const uint16_t expected_sum = image->entries[i].checksum;

            char message[160];
            snprintf(message, sizeof(message), "%s entry %u [%s]",
                     CORPUS[c], (unsigned)(i + 1), stored.c_str());

            std::vector<uint8_t> bytes = readEntry(image, mstr::toUTF8(stored));

            TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected_size, (uint32_t)bytes.size(), message);
            TEST_ASSERT_EQUAL_HEX16_MESSAGE(expected_sum, spySum(bytes), message);
            checked++;
        }

        g_src->close();
        g_src = nullptr;
    }

    // The corpus holds 67 entries. Asserting the total stops a future corpus
    // reorganisation from silently reducing this to a walk over nothing.
    TEST_ASSERT_EQUAL_UINT32(67, (uint32_t)checked);
}

// The stream reports the size it will actually serve, and serves exactly it.
void test_reading_an_entry_serves_exactly_its_bytes(void)
{
    auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[1], g_src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/spy");

    TEST_ASSERT_TRUE(image->readHeader());

    const uint32_t expected = image->entries[0].size;
    std::string wanted = mstr::toUTF8(image->entries[0].filename);
    TEST_ASSERT_TRUE(image->seekPath(wanted));
    TEST_ASSERT_EQUAL_UINT32(expected, image->size());

    std::vector<uint8_t> got;
    uint8_t buffer[256];
    for (int guard = 0; guard < 65536; guard++)
    {
        uint32_t n = image->read(buffer, sizeof(buffer));
        if (n == 0)
            break;
        got.insert(got.end(), buffer, buffer + n);
    }

    TEST_ASSERT_EQUAL_UINT32(expected, (uint32_t)got.size());
}

// The CBM file type is what a listing shows.
void test_file_types_decode(void)
{
    auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[0], g_src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/spy");

    TEST_ASSERT_EQUAL_STRING("seq", image->decodeType(0x81).c_str());
    TEST_ASSERT_EQUAL_STRING("prg", image->decodeType(0x82).c_str());
    TEST_ASSERT_EQUAL_STRING("usr", image->decodeType(0x83).c_str());
}

// Something that is not a SPYne has to be refused rather than walked. The
// format has no signature, so the check is that the first directory entry is
// structurally a directory entry.
void test_non_spyne_is_refused(void)
{
    const std::string path = "build_spy_not_a_spy.bin";

    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    std::vector<uint8_t> junk(8192);
    for (size_t i = 0; i < junk.size(); i++)
        junk[i] = (uint8_t)(0x40 + (i % 7));
    fwrite(junk.data(), 1, junk.size(), fp);
    fclose(fp);

    g_src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(g_src->isOpen());
    auto image = std::make_shared<TestSPYStream>(g_src);
    image->mode = std::ios_base::in;

    TEST_ASSERT_FALSE(image->readHeader());
}

// A container too short to hold a directory must not be walked either.
void test_truncated_container_is_refused(void)
{
    const std::string path = "build_spy_not_a_spy.bin";

    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    uint8_t stub[64] = { 0xA7, 0x02 };
    fwrite(stub, 1, sizeof(stub), fp);
    fclose(fp);

    g_src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(g_src->isOpen());
    auto image = std::make_shared<TestSPYStream>(g_src);
    image->mode = std::ios_base::in;

    TEST_ASSERT_FALSE(image->readHeader());
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_directory_lists_entries);
    RUN_TEST(test_ninth_entry_starts_at_the_next_directory_block);
    RUN_TEST(test_entries_tile_the_container);
    RUN_TEST(test_entries_are_found_by_name);
    RUN_TEST(test_reading_an_entry_serves_exactly_its_bytes);
    RUN_TEST(test_file_types_decode);
    RUN_TEST(test_non_spyne_is_refused);
    RUN_TEST(test_truncated_container_is_refused);
    RUN_TEST(test_every_entry_matches_its_stored_checksum);

    return UNITY_END();
}
