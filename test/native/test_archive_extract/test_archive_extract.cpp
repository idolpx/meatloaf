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
// Records every position the archive layer asks the source to seek to.
class SeekRecordingStream : public FileContainerStream
{
public:
    explicit SeekRecordingStream(const std::string& path) : FileContainerStream(path) {}

    bool seek(uint32_t pos) override
    {
        seeks.push_back(pos);
        return FileContainerStream::seek(pos);
    }

    bool sawSeekTo(uint32_t pos) const
    {
        for (uint32_t p : seeks)
            if (p == pos)
                return true;
        return false;
    }

    std::vector<uint32_t> seeks;
};

static bool probedTrailer(const std::string& reportedUrl)
{
    auto src = std::make_shared<SeekRecordingStream>(GZ_PATH);
    TEST_ASSERT_TRUE(src->isOpen());
    const uint32_t sz = src->size();
    if (!reportedUrl.empty())
        src->url = reportedUrl;

    WalkableArchiveStream stream(src);
    stream.seekPath("*");
    return src->sawSeekTo(sz - 4);   // the gzip ISIZE trailer probe
}

// The trailer probe seeks to EOF-4 and back to learn a .gz's decompressed size
// without decompressing it. It was briefly disabled for network sources,
// because that seek appeared to be what corrupted them: the probe read ASCII
// "core" instead of ISIZE, and the re-open after it lost the gzip filter, so a
// 174848-byte D64 was measured as its 52223-byte compressed length.
//
// The seek was not the fault. esp-idf#18359 was: esp_http_client_prepare() did
// not reset the response buffer, so any re-request parsed the PREVIOUS
// response's leftovers. With that patched (see patch_framework.py) the probe is
// safe again, and it is worth having - without it a .gz served over HTTP has no
// known size until its content is read, so a directory listing shows 0.
void test_trailer_probe_used_for_a_network_source(void)
{
    writeGz(GZ_PATH);
    TEST_ASSERT_TRUE_MESSAGE(
        probedTrailer("https://zimmers.net/anonftp/pub/cbm/c64/games/probe%2btest.d64.gz"),
        "a .gz over HTTP should learn its size from the trailer");
}

void test_trailer_probe_used_for_a_local_file(void)
{
    writeGz(GZ_PATH);
    TEST_ASSERT_TRUE_MESSAGE(probedTrailer(""),
        "a local .gz should learn its size from the trailer");
}

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

// What a caller writing the decompressed file to disk should name it.
//
// The archive layer resolves the right name (gzip FNAME, or the URL basename
// percent-decoded), but that lives on the STREAM's entry. unzipx took its
// destination from the MFile's `name`, which is the raw URL basename — so a
// file whose header said `ordeal +2 100% (ntsc pal) wanderer.d64` was written
// as `ordeal%2b2100p.d64`. getDownloadFilename() is the existing hook for
// "the real name" (wget already uses it for Content-Disposition), so
// ArchiveMFile answers it with the entry it resolved.
//
// It must be asked AFTER the stream is opened: resolving the entry is what
// discovers the name, and getDecodedStream() also calls resetURL(base()),
// which empties `name`.
void test_download_filename_is_the_resolved_entry(void)
{
    FILE* fp = fopen(GZ_PATH, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite(GZ_FNAME_BYTES, 1, sizeof(GZ_FNAME_BYTES), fp);
    fclose(fp);

    ArchiveMFile file("https://zimmers.net/anonftp/pub/cbm/c64/games/dlname%2btest.d64.gz");
    auto src = std::make_shared<FileContainerStream>(GZ_PATH);
    TEST_ASSERT_TRUE(src->isOpen());
    src->url = file.url;

    auto stream = file.getDecodedStream(src);
    TEST_ASSERT_NOT_NULL(stream.get());

    TEST_ASSERT_EQUAL_STRING_MESSAGE("ordeal +2 100% (ntsc).d64",
        file.getDownloadFilename().c_str(),
        "the extracted file should be named what the archive says it is");
}


// ImageBroker::obtain() builds its cache key from newFile->sourceFile->url.
// MFSOwner::File() assigns sourceFile only on its "look up path" branch, so a
// path resolved without a container lookup comes back with sourceFile null -
// and obtain() dereferenced it, panicking the device (LoadProhibited,
// EXCVADDR 0x20, the offset of `url`). Hit by a real web.archive.org URL that
// carries a second scheme mid-path:
//   https://web.archive.org/web/20180901151341/http://vic20tapes.org/...zip
// Whatever the resolver does with an odd path, a URL must not panic the
// device: obtain() has to return null so callers take their existing
// "cannot read archive" path.
void test_image_broker_survives_a_null_source_file(void)
{
    auto image = ImageBroker::obtain<ArchiveMStream>(
        "archive",
        "https://web.archive.org/web/20180901151341/http://vic20tapes.org/taps/x.zip");

    TEST_ASSERT_NULL_MESSAGE(image.get(),
        "obtain() must return null for a file with no source, not crash");
}

// ---------------------------------------------------------------------------
// -lh1- (LHarc 1.x: 4KiB LZSS + adaptive Huffman)
//
// libarchive's lha reader only ever supported -lh0-/-lh5-/-lh6-/-lh7-; every
// -lh1- entry failed to read with "Unsupported lzh compression method -lh1-"
// while LISTING perfectly, since the sizes come from the entry headers.
// Reported from a real archive:
//
//     meatloaf[/sd/content/archive/lzh/games.lzh]# hex menu
//       archive read error 79: Unsupported lzh compression method -lh1-
//
// Every entry carries a CRC-16 of its DECOMPRESSED bytes, written by the
// original compressor, and the reader checks it at end of entry. So decoding
// all of a real archive is a bit-exactness test, not just a smoke test: a
// single wrong byte anywhere fails.
//
// The samples under .archive/ are gitignored, so these skip when absent.
struct Lh1Sample {
    const char* path;
    size_t      entries;
};
static const Lh1Sample LH1_SAMPLES[] = {
    { ".archive/archive/lzh/games.lzh", 25 },
    { ".archive/archive/lzh/Taboo.lzh", 4 },
    { ".archive/archive/lzh/Tomb.lzh", 71 },
};

static std::vector<unsigned char> readWholeFile(const char* path)
{
    std::vector<unsigned char> bytes;
    FILE* fp = fopen(path, "rb");
    if (fp == nullptr)
        return bytes;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len > 0) {
        bytes.resize((size_t)len);
        if (fread(bytes.data(), 1, bytes.size(), fp) != bytes.size())
            bytes.clear();
    }
    fclose(fp);
    return bytes;
}

// One entry's worth of what a caller sees. `read_result` is the return of the
// read that hit end of entry: that is where "LHa data CRC error" surfaces.
struct Lh1Entry {
    std::string name;
    int64_t     declared_size;
    int64_t     decoded_size;
    int         read_result;
};

// Walks an in-memory .lzh. `read_data` false skips every entry's body, which
// is the listing path; true decodes each entry to its end, which is the path
// a LOAD or an unzipx takes.
static std::string g_lzh_error;

static std::vector<Lh1Entry> walkLzh(const std::vector<unsigned char>& bytes,
                                     bool read_data, int* walk_result)
{
    std::vector<Lh1Entry> out;
    g_lzh_error.clear();
    struct archive* a = archive_read_new();
    archive_read_support_format_lha(a);
    *walk_result = archive_read_open_memory(a, bytes.data(), bytes.size());
    if (*walk_result == ARCHIVE_OK) {
        struct archive_entry* ae;
        for (;;) {
            int r = archive_read_next_header(a, &ae);
            if (r == ARCHIVE_EOF)
                break;
            if (r < ARCHIVE_OK) {
                *walk_result = r;
                break;
            }
            Lh1Entry e;
            e.name = archive_entry_pathname(ae) ? archive_entry_pathname(ae) : "";
            e.declared_size = archive_entry_size(ae);
            e.decoded_size = 0;
            e.read_result = ARCHIVE_OK;
            if (read_data) {
                unsigned char buf[4096];
                for (;;) {
                    ssize_t n = archive_read_data(a, buf, sizeof(buf));
                    if (n <= 0) {
                        e.read_result = (int)n;
                        break;
                    }
                    e.decoded_size += n;
                }
            }
            out.push_back(e);
        }
    }
    if (archive_error_string(a) != nullptr)
        g_lzh_error = archive_error_string(a);
    archive_read_free(a);
    return out;
}

// Decode every entry of every -lh1- sample and let the archive's own CRCs
// judge the result.
void test_lh1_entries_decode_and_pass_their_crc(void)
{
    for (const auto& sample : LH1_SAMPLES) {
        auto bytes = readWholeFile(sample.path);
        if (bytes.empty())
            TEST_IGNORE_MESSAGE("sample missing under .archive/archive/lzh/");

        int walk = ARCHIVE_OK;
        auto entries = walkLzh(bytes, true, &walk);

        TEST_ASSERT_EQUAL_INT_MESSAGE(ARCHIVE_OK, walk, sample.path);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(sample.entries, entries.size(),
            sample.path);
        for (const auto& e : entries) {
            // 0 is the clean end of an entry's data; anything negative is an
            // error, and a CRC mismatch is reported that way.
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, e.read_result,
                (sample.path + std::string(": ") + e.name +
                 ": decode or CRC failed").c_str());
            TEST_ASSERT_EQUAL_INT64_MESSAGE(e.declared_size, e.decoded_size,
                (sample.path + std::string(": ") + e.name).c_str());
        }
    }
}

// -lh1- ends an entry on its uncompressed byte count, not on running out of
// input, so the encoder's final bit flush can be left unread. Nothing else
// consumes it - read_data_skip() returns early once the entry is finished -
// so the next header would be read from the wrong offset. Symptom: a listing
// works but reading entry 1 and then continuing finds garbage or stops short.
void test_lh1_reading_an_entry_leaves_the_walk_aligned(void)
{
    auto bytes = readWholeFile(LH1_SAMPLES[0].path);
    if (bytes.empty())
        TEST_IGNORE_MESSAGE("sample missing: .archive/archive/lzh/games.lzh");

    int listed_walk = ARCHIVE_OK, read_walk = ARCHIVE_OK;
    auto listed = walkLzh(bytes, false, &listed_walk);
    auto read = walkLzh(bytes, true, &read_walk);

    TEST_ASSERT_EQUAL_INT(ARCHIVE_OK, listed_walk);
    TEST_ASSERT_EQUAL_INT(ARCHIVE_OK, read_walk);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(listed.size(), read.size(),
        "decoding the entries must not lose the walk");
    for (size_t i = 0; i < listed.size() && i < read.size(); i++)
        TEST_ASSERT_EQUAL_STRING_MESSAGE(listed[i].name.c_str(),
            read[i].name.c_str(),
            "entry names differ between a listing walk and a decoding walk");
}

// -lh5- must keep working: adding -lh1- touched code every method runs through
// - the window pre-fill, the state a finished match returns to, and the
// end-of-entry accounting. Nothing else in this suite decodes through the lha
// reader at all (the zip cases skip without their sample, and the gz cases go
// through gzip), so this is the only cover for that shared path.
//
// No entry counts here: the point is that a decoding walk sees exactly what a
// listing walk sees, and that every entry's own CRC-16 accepts the bytes.
// rasm.lha is the valuable one - 135 entries and one of 851804 bytes, which
// flushes the 128KiB window six times, and that flush is where the
// shared-state edit shows up. BountyHunter and rasm mix -lh5- with stored
// -lh0- entries, so lha_read_data_none is covered alongside.
//
// mce.lha is the Amiga case: a 997-entry level-1 archive libarchive used to
// decline outright ("Unrecognized archive format", zero entries listed),
// because lha_check_header_format() demanded H_ATTR_OFFSET == 0x20 for header
// levels 1-3 and Amiga LhA writes AmigaOS protection bits (0x02) in that byte.
// It is also 3.4MB, so it is the sample that exercises the level-1 header path
// and the extended headers that come with it.
void test_lh5_entries_still_decode_and_pass_their_crc(void)
{
    static const Lh1Sample samples[] = {
        { ".archive/archive/lha/Bonanza.lha", 6 },
        { ".archive/archive/lha/BountyHunter.lha", 5 },
        { ".archive/archive/lha/LoveThisNow.lha", 9 },
        { ".archive/archive/lha/MorbidArt3fx.lha", 4 },
        { ".archive/archive/lha/rasm.lha", 135 },
        { ".archive/archive/lha/mce.lha", 997 },
    };
    size_t tested = 0;
    for (const auto& sample : samples) {
        const char* path = sample.path;
        auto bytes = readWholeFile(path);
        if (bytes.empty())
            continue;
        tested++;

        int listed_walk = ARCHIVE_OK, read_walk = ARCHIVE_OK;
        auto listed = walkLzh(bytes, false, &listed_walk);
        auto read = walkLzh(bytes, true, &read_walk);

        std::string ctx = std::string(path) + ": " + g_lzh_error +
            " (listed " + std::to_string(listed.size()) + ", read " +
            std::to_string(read.size()) + ")";
        TEST_ASSERT_EQUAL_INT_MESSAGE(ARCHIVE_OK, listed_walk, ctx.c_str());
        TEST_ASSERT_EQUAL_INT_MESSAGE(ARCHIVE_OK, read_walk, ctx.c_str());
        TEST_ASSERT_EQUAL_size_t_MESSAGE(sample.entries, read.size(), path);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(listed.size(), read.size(), path);
        for (size_t i = 0; i < read.size(); i++) {
            std::string where = std::string(path) + ": " + read[i].name;
            TEST_ASSERT_EQUAL_STRING_MESSAGE(listed[i].name.c_str(),
                read[i].name.c_str(), where.c_str());
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, read[i].read_result,
                (where + ": decode or CRC failed").c_str());
            TEST_ASSERT_EQUAL_INT64_MESSAGE(read[i].declared_size,
                read[i].decoded_size, where.c_str());
        }
    }
    if (tested == 0)
        TEST_IGNORE_MESSAGE("no samples under .archive/archive/lha/");
}

// Same alignment property, forced. The samples above cannot reach the case
// they guard: the bit reader fills a 64-bit cache greedily, so it has already
// taken the encoder's trailing flush and there is nothing left over. Make the
// two sizes disagree - shrink entry 1's declared uncompressed size - and the
// leftover compressed bytes have to be skipped anyway, or every later header
// is read from the wrong offset.
void test_lh1_short_origsize_still_leaves_the_walk_aligned(void)
{
    auto bytes = readWholeFile(LH1_SAMPLES[0].path);
    if (bytes.empty())
        TEST_IGNORE_MESSAGE("sample missing: .archive/archive/lzh/games.lzh");

    int listed_walk = ARCHIVE_OK;
    auto listed = walkLzh(bytes, false, &listed_walk);
    TEST_ASSERT_EQUAL_INT(ARCHIVE_OK, listed_walk);

    // Level-0 header: [0] header size (of everything from byte 2 on),
    // [1] checksum over that same range, [11..14] uncompressed size.
    size_t hdrsize = bytes[0];
    bytes[11] = 64;
    bytes[12] = bytes[13] = bytes[14] = 0;
    unsigned char sum = 0;
    for (size_t i = 2; i < 2 + hdrsize; i++)
        sum = (unsigned char)(sum + bytes[i]);
    bytes[1] = sum;

    int read_walk = ARCHIVE_OK;
    auto read = walkLzh(bytes, true, &read_walk);

    // Entry 1 now fails its CRC - it was cut short on purpose. Every entry
    // after it must still be found, in the same order.
    TEST_ASSERT_TRUE_MESSAGE(read.size() > 0, "header should still parse");
    TEST_ASSERT_TRUE_MESSAGE(read[0].read_result < 0,
        "a truncated entry should report its CRC error");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(listed.size(), read.size(),
        "leftover compressed bytes were not skipped: the walk lost alignment");
    for (size_t i = 1; i < listed.size() && i < read.size(); i++) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(listed[i].name.c_str(),
            read[i].name.c_str(), "walk lost alignment after the short entry");
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, read[i].read_result,
            "entries after the short one must still decode cleanly");
    }
}

// The reader must still refuse a method it has no decoder for, rather than
// e.g. producing zero bytes and claiming success. -lh2- shares -lh1-'s
// adaptive literal tree but codes positions adaptively too, which is not
// implemented; forge one from a real archive by renaming the method.
void test_unimplemented_lzh_method_is_still_refused(void)
{
    auto bytes = readWholeFile(LH1_SAMPLES[0].path);
    if (bytes.empty())
        TEST_IGNORE_MESSAGE("sample missing: .archive/archive/lzh/games.lzh");

    // Level-0 header: [0] size, [1] checksum, [2..6] "-lhN-", so the method
    // digit is byte 5. The checksum covers bytes 2.. so patch it too, to keep
    // the header itself valid.
    TEST_ASSERT_EQUAL_UINT8('1', bytes[5]);
    bytes[5] = '2';
    bytes[1] = (unsigned char)(bytes[1] + 1);

    int walk = ARCHIVE_OK;
    auto entries = walkLzh(bytes, true, &walk);

    TEST_ASSERT_TRUE_MESSAGE(entries.size() > 0, "header should still parse");
    TEST_ASSERT_TRUE_MESSAGE(entries[0].read_result < 0,
        "an unsupported method must report an error, not succeed empty");
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_zip_walk_lists_real_entries);
    RUN_TEST(test_misaligned_zip_is_not_extracted_as_raw_data);
    RUN_TEST(test_unreadable_zip_fails_rather_than_succeeding_with_junk);
    RUN_TEST(test_gz_still_reaches_its_content_through_raw);
    RUN_TEST(test_gz_size_is_the_decompressed_length);
    RUN_TEST(test_trailer_probe_used_for_a_network_source);
    RUN_TEST(test_trailer_probe_used_for_a_local_file);
    RUN_TEST(test_gzip_header_name_parser);
    RUN_TEST(test_gzip_fname_is_preferred_over_the_url);
    RUN_TEST(test_url_encoded_entry_name_is_decoded);
    RUN_TEST(test_percent_in_local_path_is_left_alone);
    RUN_TEST(test_download_filename_is_the_resolved_entry);
    RUN_TEST(test_image_broker_survives_a_null_source_file);
    RUN_TEST(test_lh1_entries_decode_and_pass_their_crc);
    RUN_TEST(test_lh5_entries_still_decode_and_pass_their_crc);
    RUN_TEST(test_lh1_reading_an_entry_leaves_the_walk_aligned);
    RUN_TEST(test_lh1_short_origsize_still_leaves_the_walk_aligned);
    RUN_TEST(test_unimplemented_lzh_method_is_still_refused);
    return UNITY_END();
}
