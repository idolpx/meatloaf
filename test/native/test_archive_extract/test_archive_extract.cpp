// Format-selection tests for the libarchive-backed archive layer.
//
// unzipx (and every other caller of ArchiveMFile::extractAll) walks an archive
// by opening it through Archive::open() and then calling nextEntrySimple()
// until it returns false. Everything those callers do afterwards depends on
// libarchive having picked the RIGHT format bidder.
//
// The failure these tests exist for: Archive::open() registers
// archive_read_support_format_raw() alongside the real container format.
// raw is a catch-all that bids 1 on literally any byte stream and synthesises
// a single entry named "data" spanning the whole input. So when the real
// format's bidder declines - because the source stream is not positioned at
// the archive's first byte, or the bytes are not the format the extension
// claims - raw wins instead of the open failing, and the caller cheerfully
// "extracts" one bogus file that is a copy of the raw input. Observed in the
// field as:
//
//     meatloaf[/sd/.bin]# unzipx https://.../Donnie_Russell_II_d64.zip
//       /sd/.bin/data  (0 bytes)
//     unzipx: extracted 1 entries, 303509 bytes to '/sd/.bin'
//
// 303509 bytes is the size of the .zip itself. Nothing was extracted; the
// archive was copied and named "data".

#include <unity.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <cstring>

#include "../test_disk_write/file_container_stream.h"
#include "media/archive/archive.h"

// A real multi-entry zip. Samples under .archive/ are gitignored, so tests
// that need one skip when it isn't there (same convention as the container
// entry tests). Paths are relative to the repo root, which is pio test's cwd.
static const char* ZIP_PATH = ".archive/zip/Donnie_Russell_II_d64.zip";

// Artifacts this suite writes; see tearDown().
static const char* GZ_PATH = "build_test_archive.gz";
static const char* GZ_BAD_ISIZE_PATH = "build_test_archive_badisize.gz";
static const char* GZ_PCT_PATH = "build_test_archive%2bplus.gz";

// nextEntrySimple()/entry are protected - extractAll() reaches them as a
// friend of ArchiveMStream. Widen access rather than duplicating the walk,
// so these tests drive exactly the code extractAll drives.
class WalkableArchiveStream : public ArchiveMStream
{
public:
    using ArchiveMStream::ArchiveMStream;
    using ArchiveMStream::entry;
    using ArchiveMStream::nextEntrySimple;
    using ArchiveMStream::open;
    using ArchiveMStream::seekPath;
};

// A source stream that starts `skew` bytes into the file and hides it: seek(p)
// lands on p+skew and position() still reports p. That is what a network
// source looks like when its response body is not aligned to the byte the
// archive layer believes it is reading - the archive layer has no way to tell,
// which is exactly why the format bid, not the caller, has to catch it.
class SkewedStream : public FileContainerStream
{
public:
    SkewedStream(const std::string& path, uint32_t skew)
        : FileContainerStream(path), m_skew(skew) {}

    bool seek(uint32_t pos) override { return FileContainerStream::seek(pos + m_skew); }

private:
    uint32_t m_skew;
};

static bool haveFile(const char* path)
{
    FILE* fp = fopen(path, "rb");
    if (fp == nullptr)
        return false;
    fclose(fp);
    return true;
}

// Artifacts follow the disk-write suite's "build_*" convention. They are
// removed HERE rather than at the end of each test: tearDown() runs after the
// test function's locals are destroyed, and on Windows remove() fails while
// the stream still holds the file open. Removing here also survives a failed
// assertion, which aborts the test body before any inline cleanup.
void setUp(void) {}
void tearDown(void)
{
    remove(GZ_PATH);
    remove(GZ_BAD_ISIZE_PATH);
    remove(GZ_PCT_PATH);
}

// Collects every entry name a caller would see from one extractAll-style walk.
static std::vector<std::string> walkEntries(std::shared_ptr<MStream> src, bool* openedOut)
{
    std::vector<std::string> names;
    WalkableArchiveStream stream(src);
    bool opened = stream.open(std::ios_base::in);
    if (openedOut != nullptr)
        *openedOut = opened;
    if (!opened)
        return names;
    while (stream.nextEntrySimple())
        names.push_back(stream.entry.filename);
    return names;
}

// Baseline: the walk must find the archive's real entries. This is what
// guards the fix from being "make everything fail".
void test_zip_walk_lists_real_entries(void)
{
    if (!haveFile(ZIP_PATH))
        TEST_IGNORE_MESSAGE("sample missing: .archive/zip/Donnie_Russell_II_d64.zip");

    auto src = std::make_shared<FileContainerStream>(ZIP_PATH);
    TEST_ASSERT_TRUE(src->isOpen());

    bool opened = false;
    auto names = walkEntries(src, &opened);

    TEST_ASSERT_TRUE_MESSAGE(opened, "archive failed to open");
    TEST_ASSERT_GREATER_THAN_MESSAGE(1, names.size(), "expected many entries");
    TEST_ASSERT_EQUAL_STRING("ADVENTURE.D64", names[0].c_str());
}

// The regression. A misaligned source must not silently become one entry
// named "data" - that is libarchive's raw catch-all reader, and reporting it
// as a successful extraction is what wrote a copy of the zip to the SD card.
void test_misaligned_zip_is_not_extracted_as_raw_data(void)
{
    if (!haveFile(ZIP_PATH))
        TEST_IGNORE_MESSAGE("sample missing: .archive/zip/Donnie_Russell_II_d64.zip");

    auto src = std::make_shared<SkewedStream>(ZIP_PATH, 64);
    TEST_ASSERT_TRUE(src->isOpen());

    bool opened = false;
    auto names = walkEntries(src, &opened);

    for (const auto& n : names)
        TEST_ASSERT_FALSE_MESSAGE(n == "data",
            "raw fallback served the whole container as one entry named 'data'");
}

// Same defect stated the way a caller experiences it: a .zip that cannot be
// read as a zip must fail, not succeed with fabricated content.
void test_unreadable_zip_fails_rather_than_succeeding_with_junk(void)
{
    if (!haveFile(ZIP_PATH))
        TEST_IGNORE_MESSAGE("sample missing: .archive/zip/Donnie_Russell_II_d64.zip");

    auto src = std::make_shared<SkewedStream>(ZIP_PATH, 64);
    bool opened = false;
    auto names = walkEntries(src, &opened);

    TEST_ASSERT_TRUE_MESSAGE(names.empty() || !opened,
        "expected no entries from an unreadable archive");
}

// The other side of the fix: raw is the RIGHT answer for a single compressed
// file. A .gz holds no archive directory - the decompressed bytes are just
// bytes - so libarchive's raw reader synthesising one "data" entry is how the
// content is reached at all. Dropping raw for real containers must not drop
// it here.
//
// The fixture is written byte by byte rather than checked in so the test is
// self-contained (same convention as test_container_entries): a gzip stream
// of GZ_PAYLOAD, produced by python's gzip module.
static const char* GZ_PAYLOAD = "MEATLOAF ARCHIVE TEST PAYLOAD\n";  // x4 = 120 bytes

static const uint8_t GZ_BYTES[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xf3, 0x75, 0x75,
    0x0c, 0xf1, 0xf1, 0x77, 0x74, 0x53, 0x70, 0x0c, 0x72, 0xf6, 0xf0, 0x0c, 0x73,
    0x55, 0x08, 0x71, 0x0d, 0x0e, 0x51, 0x08, 0x70, 0x8c, 0x04, 0x0a, 0xba, 0x70,
    0xf9, 0xd2, 0x4c, 0x16, 0x00, 0x8d, 0x90, 0x92, 0xdc, 0x78, 0x00, 0x00, 0x00
};

void test_gz_still_reaches_its_content_through_raw(void)
{
    FILE* fp = fopen(GZ_PATH, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite(GZ_BYTES, 1, sizeof(GZ_BYTES), fp);
    fclose(fp);

    auto src = std::make_shared<FileContainerStream>(GZ_PATH);
    TEST_ASSERT_TRUE(src->isOpen());

    bool opened = false;
    auto names = walkEntries(src, &opened);

    TEST_ASSERT_TRUE_MESSAGE(opened, ".gz failed to open");
    TEST_ASSERT_EQUAL_MESSAGE(1, names.size(), "expected the single synthetic entry");

    (void)GZ_PAYLOAD;
}

// GZ_BYTES with the ISIZE field (last 4 bytes) corrupted, so the gzip trailer
// probe is rejected by the 8 MB sanity cap and seekEntry() falls through to
// counting decompressed bytes. That fallback is the path a truncated
// extraction came out of on hardware, so this pins its result.
//
// HONEST SCOPE: this does NOT reproduce that hardware failure. There the
// re-open before the count came back WITHOUT the gzip filter (`filter count:
// 1 / none`), so the count measured the COMPRESSED stream and reported 41448
// bytes for a 174848-byte D64. That state comes from the HTTP source serving
// something other than the file's start after a seek, and every attempt to
// model it here - including a stream that garbles the first read after a
// backward seek - failed to put the archive in it. So this test passed both
// before and after the guard added for it in seekEntry(); it is a regression
// guard on the fallback's arithmetic, not evidence the guard works.
static const uint32_t GZ_PAYLOAD_LEN = 120;   // 4 x GZ_PAYLOAD
static const uint32_t GZ_COMPRESSED_LEN = sizeof(GZ_BYTES);   // 52

void test_gz_size_is_the_decompressed_length(void)
{
    std::vector<uint8_t> bytes(GZ_BYTES, GZ_BYTES + sizeof(GZ_BYTES));
    bytes[bytes.size() - 4] = 0xFF;
    bytes[bytes.size() - 3] = 0xFF;
    bytes[bytes.size() - 2] = 0xFF;
    bytes[bytes.size() - 1] = 0xFF;

    FILE* fp = fopen(GZ_BAD_ISIZE_PATH, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite(bytes.data(), 1, bytes.size(), fp);
    fclose(fp);

    auto src = std::make_shared<FileContainerStream>(GZ_BAD_ISIZE_PATH);
    TEST_ASSERT_TRUE(src->isOpen());

    WalkableArchiveStream stream(src);
    bool found = stream.seekPath("*");
    uint32_t reported = stream.size();


    TEST_ASSERT_TRUE_MESSAGE(found, "entry not found");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GZ_PAYLOAD_LEN, reported,
        "size must be the decompressed length, not the compressed one");
}

// A single compressed file has no stored entry name, so the archive layer
// synthesises one from the container's URL. When that URL is a network URL the
// component is percent-encoded, and the encoding must not survive into the
// name a file is written under: `.../ordeal%2b2100p.d64.gz` fetched over HTTPS
// extracted to a file literally called `ordeal%2b2100p.d64` instead of
// `ordeal+2100p.d64`.
//
// alter_pluses must be false: a '+' in a PATH is a literal plus, not the
// form-encoded space it means in a query string.
static std::string entryNameFor(const std::string& fixturePath, const std::string& reportedUrl)
{
    auto src = std::make_shared<FileContainerStream>(fixturePath);
    if (!src->isOpen())
        return "<fixture missing>";
    if (!reportedUrl.empty())
        src->url = reportedUrl;   // read the local bytes, report a network URL

    // seekPath(), not nextEntrySimple(): the latter is the minimal walk used by
    // extractAll() and deliberately skips the name/size synthesis. Deriving a
    // name for a single compressed file happens in seekEntry().
    WalkableArchiveStream stream(src);
    if (!stream.seekPath("*"))
        return "<not found>";
    return stream.entry.filename;
}

static void writeGz(const char* path)
{
    FILE* fp = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite(GZ_BYTES, 1, sizeof(GZ_BYTES), fp);
    fclose(fp);
}

// A gzip header can carry the ORIGINAL filename (RFC 1952 FNAME, flag bit 3),
// and it is a better name than anything the URL gives: zimmers'
// `ordeal%2b2100p.d64.gz` carries `ordeal +2 100% (ntsc).d64`. libarchive
// surfaces FNAME as the entry pathname - but only when a format reader
// produces an entry, and for these files archive_read_next_header() returns
// ARCHIVE_EOF, so that branch never sees it and the URL is all that is left.
//
// Same payload as GZ_BYTES, written by python's gzip module with filename set.
static const uint8_t GZ_FNAME_BYTES[] = {
    0x1f, 0x8b, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x6f, 0x72, 0x64,
    0x65, 0x61, 0x6c, 0x20, 0x2b, 0x32, 0x20, 0x31, 0x30, 0x30, 0x25, 0x20, 0x28,
    0x6e, 0x74, 0x73, 0x63, 0x29, 0x2e, 0x64, 0x36, 0x34, 0x00, 0xf3, 0x75, 0x75,
    0x0c, 0xf1, 0xf1, 0x77, 0x74, 0x53, 0x70, 0x0c, 0x72, 0xf6, 0xf0, 0x0c, 0x73,
    0x55, 0x08, 0x71, 0x0d, 0x0e, 0x51, 0x08, 0x70, 0x8c, 0x04, 0x0a, 0xba, 0x70,
    0xf9, 0xd2, 0x4c, 0x16, 0x00, 0x8d, 0x90, 0x92, 0xdc, 0x78, 0x00, 0x00, 0x00
};

// Archive::gzipNameFromHeader() on its own. The compressed-only path that
// needs it is only reachable when archive_read_next_header() returns
// ARCHIVE_EOF, which happens on the device but not for these fixtures (here a
// raw entry is produced and libarchive reports FNAME as its pathname), so the
// parser is exercised directly.
void test_gzip_header_name_parser(void)
{
    // FNAME present.
    std::string name = Archive::gzipNameFromHeader(GZ_FNAME_BYTES, sizeof(GZ_FNAME_BYTES));
    TEST_ASSERT_EQUAL_STRING("ordeal +2 100% (ntsc).d64", name.c_str());

    // FNAME absent (GZ_BYTES has FLG=0).
    TEST_ASSERT_EQUAL_STRING("",
        Archive::gzipNameFromHeader(GZ_BYTES, sizeof(GZ_BYTES)).c_str());

    // Not a gzip stream at all.
    static const uint8_t notGz[] = { 'P', 'K', 3, 4, 0, 0, 0, 0, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL_STRING("",
        Archive::gzipNameFromHeader(notGz, sizeof(notGz)).c_str());

    // Truncated before the name terminates: must yield nothing rather than a
    // partial filename. 20 bytes cuts into "ordeal +2 ...".
    TEST_ASSERT_EQUAL_STRING("",
        Archive::gzipNameFromHeader(GZ_FNAME_BYTES, 20).c_str());

    // Too short to be a header, and null.
    TEST_ASSERT_EQUAL_STRING("", Archive::gzipNameFromHeader(GZ_FNAME_BYTES, 4).c_str());
    TEST_ASSERT_EQUAL_STRING("", Archive::gzipNameFromHeader(nullptr, 100).c_str());
}

void test_gzip_fname_is_preferred_over_the_url(void)
{
    FILE* fp = fopen(GZ_PATH, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite(GZ_FNAME_BYTES, 1, sizeof(GZ_FNAME_BYTES), fp);
    fclose(fp);

    std::string name = entryNameFor(
        GZ_PATH, "https://zimmers.net/anonftp/pub/cbm/c64/games/ordeal%2b2100p.d64.gz");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("ordeal +2 100% (ntsc).d64", name.c_str(),
        "the name stored in the gzip header should win over the URL basename");
}

void test_url_encoded_entry_name_is_decoded(void)
{
    // A DIFFERENT URL from the FNAME test above on purpose: seekPath() caches
    // the resolved entry in an ArchiveMSession keyed on the container URL, and
    // SessionBroker state outlives a single test. Reusing the URL returns the
    // previous test's cached entry instead of resolving this fixture.
    writeGz(GZ_PATH);   // GZ_BYTES has no FNAME, so the URL is the only source
    std::string name = entryNameFor(
        GZ_PATH, "https://zimmers.net/anonftp/pub/cbm/c64/games/plain%2bname.d64.gz");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("plain+name.d64", name.c_str(),
        "percent-encoding from the URL leaked into the extracted filename");
}

// The other half: a LOCAL path is not URL-encoded, so a file genuinely named
// with a '%' keeps it. Decoding unconditionally would rename it.
void test_percent_in_local_path_is_left_alone(void)
{
    writeGz(GZ_PCT_PATH);
    std::string name = entryNameFor(GZ_PCT_PATH, "");

    TEST_ASSERT_EQUAL_STRING_MESSAGE("build_test_archive%2bplus", name.c_str(),
        "a local filename containing '%' must not be percent-decoded");
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_zip_walk_lists_real_entries);
    RUN_TEST(test_misaligned_zip_is_not_extracted_as_raw_data);
    RUN_TEST(test_unreadable_zip_fails_rather_than_succeeding_with_junk);
    RUN_TEST(test_gz_still_reaches_its_content_through_raw);
    RUN_TEST(test_gz_size_is_the_decompressed_length);
    RUN_TEST(test_gzip_header_name_parser);
    RUN_TEST(test_gzip_fname_is_preferred_over_the_url);
    RUN_TEST(test_url_encoded_entry_name_is_decoded);
    RUN_TEST(test_percent_in_local_path_is_left_alone);
    return UNITY_END();
}
