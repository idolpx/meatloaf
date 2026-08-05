#include <unity.h>
#include <cstdio>
#include <memory>
#include "media/disk/d64.h"
#include "media/disk/d71.h"
#include "media/disk/d80.h"
#include "media/disk/d81.h"
#include "media/disk/d82.h"
#include "file_container_stream.h"
#include "c1541_oracle.h"

void setUp(void) {}
void tearDown(void) {}

void test_file_container_stream_roundtrip(void)
{
    const char* path = "build_test_fcs.bin";
    remove(path);
    {
        FileContainerStream s(path, 1024);
        TEST_ASSERT_TRUE(s.isOpen());
        TEST_ASSERT_EQUAL_UINT32(1024, s.size());

        const uint8_t out[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        TEST_ASSERT_TRUE(s.seek(256));
        TEST_ASSERT_EQUAL_UINT32(4, s.write(out, 4));

        uint8_t in[4] = { 0, 0, 0, 0 };
        TEST_ASSERT_TRUE(s.seek(256));
        TEST_ASSERT_EQUAL_UINT32(4, s.read(in, 4));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(out, in, 4);
    }
    // Reopening must see the persisted bytes and the original size.
    {
        FileContainerStream s(path);
        TEST_ASSERT_EQUAL_UINT32(1024, s.size());
        uint8_t in[4] = { 0, 0, 0, 0 };
        TEST_ASSERT_TRUE(s.seek(256));
        TEST_ASSERT_EQUAL_UINT32(4, s.read(in, 4));
        TEST_ASSERT_EQUAL_UINT8(0xDE, in[0]);
        TEST_ASSERT_EQUAL_UINT8(0xEF, in[3]);
    }
    remove(path);
}

// Proves the write engine compiles, links, and constructs natively, by reading
// values that only exist if its geometry tables were built.
//
// speedZone(track) = (track<18) + (track<25) + (track<31), indexing
// sectorsPerTrack = {17, 18, 19, 21}. That maps: tracks 1-17 -> 21 sectors,
// tracks 18-24 -> 19, tracks 25-30 -> 18, tracks 31-35 -> 17 (verified against
// d64.h; the brief's placeholder values for tracks 25 and 31 didn't match the
// code and are corrected here per the brief's own instruction to do so).
void test_engine_constructs_with_expected_geometry(void)
{
    const char* path = "build_test_geom.d64";
    remove(path);
    auto src = std::make_shared<FileContainerStream>(path, 174848);
    D64MStream image(src);

    TEST_ASSERT_EQUAL_UINT16(21, image.getSectorCount(1));   // zone 1 (tracks 1-17)
    TEST_ASSERT_EQUAL_UINT16(18, image.getSectorCount(25));  // zone 3 (tracks 25-30)
    TEST_ASSERT_EQUAL_UINT16(17, image.getSectorCount(31));  // zone 4 (tracks 31-35)
    TEST_ASSERT_EQUAL_UINT16(17, image.getSectorCount(35));  // zone 4 (tracks 31-35)
    TEST_ASSERT_EQUAL_UINT8(18, image.partitions[image.partition].directory_track);

    // Close the underlying file before removing it - on Windows an open
    // handle (still held via src's shared_ptr) blocks remove().
    src->close();
    remove(path);
}

void test_default_image_sizes(void)
{
    const char* path = "build_test_sizes.bin";
    remove(path);
    auto src = std::make_shared<FileContainerStream>(path, 256);

    TEST_ASSERT_EQUAL_UINT32(174848,  D64MStream(src).defaultImageSize());
    TEST_ASSERT_EQUAL_UINT32(349696,  D71MStream(src).defaultImageSize());
    TEST_ASSERT_EQUAL_UINT32(533248,  D80MStream(src).defaultImageSize());
    TEST_ASSERT_EQUAL_UINT32(819200,  D81MStream(src).defaultImageSize());
    TEST_ASSERT_EQUAL_UINT32(1066496, D82MStream(src).defaultImageSize());

    remove(path);
}

void test_format_image_creates_sized_image(void)
{
    const char* path = "build_test_fmt.d64";
    remove(path);
    {
        auto src = std::make_shared<FileContainerStream>(path, 174848);
        D64MStream image(src);
        TEST_ASSERT_TRUE(image.formatImage("testdisk", "01"));
    }
    FILE* fp = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    fseek(fp, 0, SEEK_END);
    TEST_ASSERT_EQUAL_UINT32(174848, (uint32_t)ftell(fp));
    fclose(fp);
    remove(path);
}

// FIX ROUND 1 (see task-5-report.md): the original c1541_validate() judged
// success/failure by scanning `-validate`'s text output for "error"/"wrong".
// But `-validate` REPAIRS BAM-vs-chain mismatches silently - no diagnostic
// text, exit code 0 - so that check could never detect the exact corruption
// class this suite exists to catch. c1541_validate() now byte-diffs the
// image before/after; any difference means validate had to fix something.
//
// Running that corrected check against our own formatImage() output (write
// a real file, then validate) surfaced a REAL, previously unknown defect
// (finding #2, confirmed independently by the reviewer):
// D64MStream::initializeBlockAllocationMap() (lib/meatloaf/media/disk/d64.h)
// marks every sector of the header/directory track (track 18) as BAM-free,
// including the two sectors formatImage() itself just used - 18/0 (header/
// BAM sector) and 18/1 (first directory sector). c1541's first validate
// pass reserves them (free count 19->17, bitmap byte 0xFF->0xFC for
// track 18's own BAM record), which the byte-diff correctly reports as an
// inconsistent image. This is a genuine finding, not a false positive from
// the new detector: on real hardware those two sectors would eventually be
// handed out to a file by getNextFreeBlock(), corrupting the header/
// directory. Per this task's constraints, the engine is NOT fixed here.
//
// FIX ROUND 2: an inverted TEST_ASSERT_FALSE previously stood in for this
// (encoding the current broken behavior as "pass"), but that makes green
// CI indistinguishable from "actually fixed" without reading this comment.
// This plan's convention for a test blocked on a known finding is
// TEST_IGNORE_MESSAGE naming it - used below instead. The setup (format +
// real write + the validate call) is left in place and its result computed
// so the test is immediately live the moment the ignore line is removed.
void test_c1541_validates_our_formatted_image(void)
{
    if (!c1541_available())
        TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");

    const char* path = "build_test_oracle.d64";
    remove(path);
    {
        auto src = std::make_shared<FileContainerStream>(path, 174848);
        D64MStream image(src);
        TEST_ASSERT_TRUE(image.formatImage("testdisk", "01"));

        // A file write is needed to reproduce the finding above: an empty
        // freshly-formatted image never calls getNextFreeBlock(), so the
        // header/directory track's BAM entries are read but never compared
        // against anything until a real allocation walks near them.
        image.mode = std::ios_base::out;
        TEST_ASSERT_TRUE(image.seekPath("REGRESS"));
        std::vector<uint8_t> payload(600, 0x41);
        TEST_ASSERT_EQUAL_UINT32(600, image.writeFile(payload.data(), (uint32_t)payload.size()));
        image.close();
    }

    bool valid = c1541_validate(path);
    remove(path);

    TEST_IGNORE_MESSAGE("finding #2: initializeBlockAllocationMap() never reserves the "
                        "header/directory sectors, so formatImage() output does not pass "
                        "c1541 validate; re-enable when that is fixed");

    TEST_ASSERT_TRUE_MESSAGE(valid, "c1541 validate rejected our formatted image");
}

// Positive-path proof for c1541_validate(): a c1541-formatted image (not
// ours) must validate clean, so the wrapper is shown to return TRUE for a
// genuinely consistent image and not just FALSE for everything.
void test_c1541_validates_a_c1541_formatted_image(void)
{
    if (!c1541_available())
        TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");

    const char* path = "build_test_oracle_ref.d64";
    remove(path);
    c1541_run("-format " + c1541_quote("refdisk,01") + " d64 " + c1541_quote(path));

    TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path),
                             "c1541 validate rejected an image c1541 itself just formatted");
    remove(path);
}

// Regression test for the fix-round-1 defect: c1541_validate() must detect
// a BAM-vs-chain inconsistency even when c1541 prints nothing about it.
//
// FIX ROUND 2: this originally built its image with our own formatImage(),
// but that output is ALREADY BAM-inconsistent before any deliberate
// corruption (finding #2, see test_c1541_validates_our_formatted_image) -
// validate would repair the pre-existing track-18 defect in the same pass
// as the deliberate bit flip below, so `before != after` held regardless of
// whether the deliberate corruption logic worked at all. The test would
// have kept passing even with the corruption code deleted outright.
//
// Made engine-independent instead: the image is built and populated with
// c1541 itself (-format, then -write for a real file, so a genuine T/S
// chain + BAM allocation exists), and the baseline is asserted TRUE before
// any corruption is applied - the step that makes the rest meaningful. Only
// then is one BAM bitmap bit for an allocated block flipped back to "free"
// while its directory/chain still references that block (the reviewer's
// exact repro), with a final assertion that c1541_validate() now catches it.
void test_c1541_validate_detects_bam_chain_corruption(void)
{
    if (!c1541_available())
        TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");

    const char* path = "build_test_oracle_corrupt.d64";
    const char* srcPath = "build_test_oracle_corrupt_src.bin";
    remove(path);
    remove(srcPath);

    // Header/directory track (18) sector 0 byte offset: pure per-track
    // geometry (no I/O), so a throwaway D64MStream/FileContainerStream over
    // a not-yet-existing path is fine just to call getSectorCount().
    uint32_t bamOffset = 0;
    {
        auto probeSrc = std::make_shared<FileContainerStream>("build_test_oracle_corrupt_probe.tmp", 256);
        D64MStream probe(probeSrc);
        for (uint8_t t = 1; t < 18; t++)
            bamOffset += (uint32_t)probe.getSectorCount(t) * 256;
        probeSrc->close();
        remove("build_test_oracle_corrupt_probe.tmp");
    }

    c1541_run("-format " + c1541_quote("regress,01") + " d64 " + c1541_quote(path));

    uint8_t bamBefore[256] = {0};
    {
        FILE* fp = fopen(path, "rb");
        TEST_ASSERT_NOT_NULL(fp);
        fseek(fp, (long)bamOffset, SEEK_SET);
        TEST_ASSERT_EQUAL_UINT32(256, (uint32_t)fread(bamBefore, 1, 256, fp));
        fclose(fp);
    }

    // Write a real multi-block file via c1541 itself so a genuine T/S chain
    // + BAM allocation exists to corrupt.
    {
        FILE* fp = fopen(srcPath, "wb");
        TEST_ASSERT_NOT_NULL(fp);
        std::vector<uint8_t> payload(600, 0x41);
        TEST_ASSERT_EQUAL_UINT32(600, (uint32_t)fwrite(payload.data(), 1, payload.size(), fp));
        fclose(fp);
    }
    c1541_run("-attach " + c1541_quote(path) + " -write " + c1541_quote(srcPath) + " " + c1541_quote("REGRESS"));

    // The baseline must itself validate clean before corrupting it - this
    // is the step that makes the assertion below meaningful. Without it, a
    // broken bit-flip below would leave `before == after` and the test
    // would fail here instead of silently passing for the wrong reason.
    TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path),
                             "baseline (c1541-formatted + c1541-written file) did not "
                             "validate clean - cannot draw any conclusion from corrupting it");

    uint8_t bamAfter[256] = {0};
    {
        FILE* fp = fopen(path, "rb");
        TEST_ASSERT_NOT_NULL(fp);
        fseek(fp, (long)bamOffset, SEEK_SET);
        TEST_ASSERT_EQUAL_UINT32(256, (uint32_t)fread(bamAfter, 1, 256, fp));
        fclose(fp);
    }

    // Find one BAM bitmap bit (not a per-track free-count byte, which sits
    // at offset 4 + (track-1)*4) that flipped from free(1) to allocated(0) -
    // i.e. a real block the file write actually claimed.
    int flipByte = -1;
    uint8_t flipBit = 0;
    for (int i = 4; i < 144 && flipByte < 0; i++)
    {
        if (((i - 4) % 4) == 0) continue; // skip the per-track free-count byte
        uint8_t clearedBits = (uint8_t)(bamBefore[i] & (uint8_t)~bamAfter[i]);
        if (clearedBits != 0)
        {
            flipByte = i;
            for (uint8_t b = 0; b < 8; b++)
            {
                if (clearedBits & (1 << b)) { flipBit = (uint8_t)(1 << b); break; }
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(flipByte >= 0, "writing a file did not allocate any BAM bitmap bit");

    // Corrupt the image: mark that allocated, chain-referenced block "free"
    // in the BAM - a textbook BAM-vs-chain inconsistency.
    {
        FILE* fp = fopen(path, "r+b");
        TEST_ASSERT_NOT_NULL(fp);
        fseek(fp, (long)(bamOffset + (uint32_t)flipByte), SEEK_SET);
        uint8_t corrupted = (uint8_t)(bamAfter[flipByte] | flipBit);
        TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)fwrite(&corrupted, 1, 1, fp));
        fclose(fp);
    }

    TEST_ASSERT_FALSE_MESSAGE(c1541_validate(path),
                              "c1541 validate did not flag a BAM bit that falsely marks an "
                              "allocated, chain-referenced block as free");
    remove(path);
    remove(srcPath);
}

// Regression test for the fix-round-1 c1541_read() defect: a mid-chain
// error (bad T/S link) made c1541 write a truncated output file and print
// an error, but the old c1541_read() only checked "did fopen() succeed" and
// reported success anyway.
void test_c1541_read_detects_truncated_read(void)
{
    if (!c1541_available())
        TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");

    const char* path = "build_test_oracle_trunc.d64";
    const char* outPath = "build_test_oracle_trunc.prg";
    remove(path);
    remove(outPath);

    {
        auto src = std::make_shared<FileContainerStream>(path, 174848);
        D64MStream image(src);
        TEST_ASSERT_TRUE(image.formatImage("regress", "01"));

        image.mode = std::ios_base::out;
        TEST_ASSERT_TRUE(image.seekPath("REGRESS"));
        std::vector<uint8_t> payload(600, 0x41); // 3 blocks - a real chain to break
        TEST_ASSERT_EQUAL_UINT32(600, image.writeFile(payload.data(), (uint32_t)payload.size()));
        image.close(); // finalizes the write and closes the container
    }

    // Fresh probe over the finished image. getSectorCount() is pure
    // per-track geometry (no I/O against the file), reused here to convert
    // track/sector to a byte offset without touching any protected
    // partition internals.
    auto probeSrc = std::make_shared<FileContainerStream>(path);
    D64MStream probe(probeSrc);
    auto sectorOffset = [&](uint8_t trk, uint8_t sec) -> uint32_t {
        uint32_t off = 0;
        for (uint8_t t = 1; t < trk; t++)
            off += (uint32_t)probe.getSectorCount(t) * 256;
        return off + (uint32_t)sec * 256;
    };

    // File lands in directory slot 0 on a freshly formatted disk;
    // start_track/start_sector are entry bytes 3/4 of the first directory
    // sector (18/1).
    uint8_t entry[5] = {0};
    TEST_ASSERT_TRUE(probeSrc->seek(sectorOffset(18, 1)));
    TEST_ASSERT_EQUAL_UINT32(5, probeSrc->read(entry, 5));
    uint8_t startTrack = entry[3];
    uint8_t startSector = entry[4];
    TEST_ASSERT_NOT_EQUAL(0, startTrack);

    uint32_t firstBlockOffset = sectorOffset(startTrack, startSector);
    probeSrc->close();

    // Corrupt the first block's T/S link to an out-of-bounds track/sector,
    // breaking the chain after the first block's payload is delivered.
    {
        FILE* fp = fopen(path, "r+b");
        TEST_ASSERT_NOT_NULL(fp);
        fseek(fp, (long)firstBlockOffset, SEEK_SET);
        uint8_t badLink[2] = {99, 99};
        TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)fwrite(badLink, 1, 2, fp));
        fclose(fp);
    }

    size_t bytesRead = 0;
    TEST_ASSERT_FALSE_MESSAGE(c1541_read(path, "REGRESS", outPath, &bytesRead),
                              "c1541_read() reported success for a chain that broke mid-read");
    TEST_ASSERT_TRUE_MESSAGE(bytesRead < 600,
                             "expected a truncated (partial) output file, got the full size");

    remove(path);
    remove(outPath);
}

void process()
{
    UNITY_BEGIN();
    RUN_TEST(test_file_container_stream_roundtrip);
    RUN_TEST(test_engine_constructs_with_expected_geometry);
    RUN_TEST(test_default_image_sizes);
    RUN_TEST(test_format_image_creates_sized_image);
    RUN_TEST(test_c1541_validates_our_formatted_image);
    RUN_TEST(test_c1541_validates_a_c1541_formatted_image);
    RUN_TEST(test_c1541_validate_detects_bam_chain_corruption);
    RUN_TEST(test_c1541_read_detects_truncated_read);
    UNITY_END();
}

int main(int argc, char **argv)
{
    process();
    return 0;
}
