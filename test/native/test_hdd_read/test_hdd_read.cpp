// Read-path tests for IDE64 CFS (.hdd) images.
//
// These run against a real image because the CFS layout (boot sector,
// partition directory, sliced NEXTS pointers, balanced data trees) is too
// intertwined to synthesize meaningfully. The image lives in .archive/, which
// is gitignored, so every test skips cleanly when it isn't present.

#include <unity.h>

#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/hd/hdd.h"

// The image is opened relative to the project root, which is where
// `pio test -e native` runs the test binary from.
static const char* IMAGE_PATH = ".archive/hdd/ide20201227.hdd";

// A file that sits three levels down: partition STUFF, then two
// subdirectories. Its size comes from the CFS directory entry.
static const char* DEEP_FILE = "STUFF/UTILS/AAY41-0.22/AA";
static const uint32_t DEEP_FILE_SIZE = 6429;

// seekPath() is protected; the tests drive it the way the drive does.
class TestHDDStream : public HDDMStream
{
public:
    using HDDMStream::HDDMStream;
    using HDDMStream::entry;
    using HDDMStream::readHeader;
    using HDDMStream::seekPath;
};

static bool imageAvailable()
{
    FILE* fp = fopen(IMAGE_PATH, "rb");
    if (fp == nullptr)
        return false;
    fclose(fp);
    return true;
}

// Returns nullptr when the sample image isn't available.
static std::shared_ptr<TestHDDStream> openImage()
{
    auto src = std::make_shared<FileContainerStream>(IMAGE_PATH);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestHDDStream>(src);
    // A directly constructed media stream has an uninitialised `mode` - only
    // MFile::getSourceStream() sets it - and garbage with the `out` bit set
    // sends seekPath() down the write branch.
    image->mode = std::ios_base::in;
    return image;
}

void setUp(void) {}
void tearDown(void) {}

// The bug: a stream that has not been through a directory listing has never
// had its boot sector or partition table parsed, so every path resolves to
// "not found". `ls` worked because HDDMFile::rewindDirectory() calls
// readHeader() explicitly; opening a file for reading goes straight to
// seekPath() on a freshly decoded stream and got nothing.
void test_seekPath_reads_header_lazily(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE_MESSAGE(image->seekPath(DEEP_FILE),
        "seekPath() failed on a stream whose header had not been read");
    TEST_ASSERT_EQUAL_UINT32(DEEP_FILE_SIZE, image->size());
}

// Isolates the cause: the identical seek succeeds once the header has been
// read, so nothing about the path itself is wrong.
void test_seekPath_succeeds_after_explicit_readHeader(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_TRUE(image->seekPath(DEEP_FILE));
    TEST_ASSERT_EQUAL_UINT32(DEEP_FILE_SIZE, image->size());
    TEST_ASSERT_EQUAL_STRING("AA", image->entry.filename.c_str());
}

// A successful seek must also hand back the file's bytes: read() returns at
// most one block, so the caller loops until it dries up.
void test_read_returns_whole_file(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekPath(DEEP_FILE));

    std::vector<uint8_t> got(DEEP_FILE_SIZE, 0);
    uint32_t n = 0;
    while (n < got.size())
    {
        uint32_t now = image->read(got.data() + n, (uint32_t)(got.size() - n));
        if (now == 0)
            break;
        n += now;
    }

    TEST_ASSERT_EQUAL_UINT32(DEEP_FILE_SIZE, n);

    // PRG load address: this file is a C64 program, so the first two bytes
    // must be a plausible non-zero little-endian address rather than the
    // $00 fill a hole in the data tree would produce.
    TEST_ASSERT_NOT_EQUAL(0, (got[0] | (got[1] << 8)));
}

// Resolving a directory (rather than a file) must not need a prior listing
// either - isDirectory()/exists() reach resolvePath() the same way.
void test_resolvePath_finds_directory_without_listing(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_EQUAL(HDDMStream::PATH_DIR, image->resolvePath("STUFF/UTILS"));
    TEST_ASSERT_EQUAL(HDDMStream::PATH_FILE, image->resolvePath(DEEP_FILE));
    TEST_ASSERT_EQUAL(HDDMStream::PATH_NOT_FOUND, image->resolvePath("STUFF/NO SUCH DIR/AA"));
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    if (!imageAvailable())
    {
        // .archive/ is gitignored, so the sample image is only present on
        // machines that have it. Skip rather than fail.
        TEST_IGNORE_MESSAGE("sample image .archive/hdd/ide20201227.hdd not present");
        return UNITY_END();
    }

    RUN_TEST(test_seekPath_reads_header_lazily);
    RUN_TEST(test_seekPath_succeeds_after_explicit_readHeader);
    RUN_TEST(test_read_returns_whole_file);
    RUN_TEST(test_resolvePath_finds_directory_without_listing);

    return UNITY_END();
}
