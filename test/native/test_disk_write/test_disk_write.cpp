#include <unity.h>
#include <random>
#include <cstdio>
#include <memory>
#include "media/disk/d64.h"
#include "media/disk/d71.h"
#include "media/disk/d80.h"
#include "media/disk/d81.h"
#include "media/disk/d82.h"
#include "file_container_stream.h"
#include "c1541_oracle.h"
#include "image_invariants.h"
#include "format_fixtures.h"

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

// Fix round 1 regression test (see task-7-report.md): c1541_validate()'s two
// detection paths (text scan for "error"/"Error"/"wrong", byte-diff
// before/after) both missed c1541's CBM error-channel report format
// ("ERR = 65, NO BLOCK, 00, 38"), which is what c1541 prints - with no bytes
// changed - when -validate hits a genuinely invalid track/sector reference
// it can't repair. That made c1541_validate() silently report an image as
// VALID when c1541 itself had just rejected it (discovered on D80/D82 Tier
// 0 output). Fixed by additionally matching the literal "ERR =" (see
// c1541_oracle.h for why not a case-insensitive "err" search).
//
// Fixture: our own D80 formatImage() output reproduces "ERR = 65, NO BLOCK"
// on c1541 -validate today, courtesy of finding #2
// (initializeBlockAllocationMap() never reserving the header/directory
// sectors - see the findings file). That makes this test depend on a known-
// broken engine path: it will stop reproducing the moment finding #2 is
// fixed, at which point it needs a replacement fixture (ideally a hand-
// corrupted c1541-built image that induces the same c1541 error report
// without relying on our engine being broken).
void test_c1541_validate_detects_cbm_error_channel_report(void)
{
    if (!c1541_available())
        TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");

    // Build a clean D80 with c1541 itself, then deliberately corrupt it, so this
    // test stays engine-independent. It originally used our own formatImage()
    // output as a ready-made fixture, which stopped reproducing the moment the
    // D80 format bugs were fixed - the fixture has to create the fault itself.
    const char* path = "build_test_oracle_errchan.d80";
    remove(path);
    c1541_run("-format " + c1541_quote("errchan,01") + " d80 " + c1541_quote(path));

    // An 8050 BAM block carries its track range at bytes 4 (lowest) and 5
    // (highest + 1). 38/0 sits at (37 tracks * 29 sectors) * 256. Pushing the
    // upper bound past the last real track (77) makes -validate walk blocks that
    // do not exist and emit "ERR = 65, NO BLOCK, ...".
    const long bam_hi_byte = (37L * 29L) * 256L + 5L;
    {
        FILE* f = fopen(path, "r+b");
        TEST_ASSERT_NOT_NULL_MESSAGE(f, "could not open the c1541-built D80 fixture");
        TEST_ASSERT_EQUAL_INT(0, fseek(f, bam_hi_byte, SEEK_SET));
        uint8_t bogus = 96; // well past track 77
        fwrite(&bogus, 1, 1, f);
        fclose(f);
    }

    bool valid = c1541_validate(path);
    remove(path);

    TEST_ASSERT_FALSE_MESSAGE(valid,
        "c1541_validate() did not detect c1541's \"ERR =\" error-channel report "
        "during -validate on a D80 image with an invalid track/sector reference");
}

// FIX ROUND 1: our own independent structural walk of a freshly formatted
// image trips immediately - directory: block 18/1 is in a chain but marked
// free in BAM. That is finding #2 again (initializeBlockAllocationMap()
// never reserves the header/BAM sector (18/0) or the first directory sector
// (18/1) that formatImage() itself just wrote into), this time caught by an
// engine-independent checker instead of by diffing c1541's repair pass. The
// checker only remembers the FIRST invariant it hits (see
// ImageInvariantChecker::fail()), so this single message doesn't rule out
// invariants 2 ("every allocated block is reachable") and 6 (blocksFree()
// vs BAM bitmap) also failing further down the same run - they never get a
// chance to report because the walk fails on the very first directory
// block. Per this task's constraints, the engine is NOT fixed here; the
// project convention (TEST_IGNORE_MESSAGE, body left intact) is used so
// this test goes live the moment finding #2 is fixed.
void test_invariants_pass_on_blank_image(void)
{
    const char* path = "build_test_inv.d64";
    remove(path);
    auto src = std::make_shared<FileContainerStream>(path, 174848);
    D64MStream image(src);
    TEST_ASSERT_TRUE(image.formatImage("testdisk", "01"));

    InvariantResult r = check_invariants(image);

    // Cleanup before the longjmp'ing calls below (TEST_IGNORE_MESSAGE and
    // TEST_ASSERT_TRUE_MESSAGE both longjmp past anything after them).
    src->close();
    remove(path);


    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message.c_str());
}

// Regression test for the FIX ROUND 1 defect described above
// (image_invariants.h): check_invariants() must not fabricate violations on
// a genuinely clean image. Built entirely with c1541 itself - format plus
// TWO real file writes - so it sidesteps findings #1/#2 in our own engine
// and isolates the checker's own logic. TWO files matter: the bug only
// showed up on directory entries read AFTER the first file's chain had
// already been walked, so a single-file image can't exercise it.
void test_invariants_pass_on_clean_c1541_image_with_two_files(void)
{
    if (!c1541_available())
        TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");

    const char* path = "build_test_inv_c1541.d64";
    const char* srcPath1 = "build_test_inv_c1541_a.bin";
    const char* srcPath2 = "build_test_inv_c1541_b.bin";
    remove(path);
    remove(srcPath1);
    remove(srcPath2);

    c1541_run("-format " + c1541_quote("regress,01") + " d64 " + c1541_quote(path));

    {
        FILE* fp = fopen(srcPath1, "wb");
        TEST_ASSERT_NOT_NULL(fp);
        std::vector<uint8_t> payload(600, 0x41); // 'A' - this is the exact
                                                  // byte pattern the reviewer's
                                                  // repro found misread as a
                                                  // directory entry (65/65).
        TEST_ASSERT_EQUAL_UINT32(600, (uint32_t)fwrite(payload.data(), 1, payload.size(), fp));
        fclose(fp);
    }
    {
        FILE* fp = fopen(srcPath2, "wb");
        TEST_ASSERT_NOT_NULL(fp);
        std::vector<uint8_t> payload(600, 0x42);
        TEST_ASSERT_EQUAL_UINT32(600, (uint32_t)fwrite(payload.data(), 1, payload.size(), fp));
        fclose(fp);
    }
    c1541_run("-attach " + c1541_quote(path) + " -write " + c1541_quote(srcPath1) + " " + c1541_quote("FILEA"));
    c1541_run("-attach " + c1541_quote(path) + " -write " + c1541_quote(srcPath2) + " " + c1541_quote("FILEB"));

    // The baseline must itself validate clean before we trust conclusions
    // drawn from running our own checker on it.
    bool valid = c1541_validate(path);

    InvariantResult r;
    {
        auto src = std::make_shared<FileContainerStream>(path);
        D64MStream image(src);
        r = check_invariants(image);
        src->close();
    }

    remove(path);
    remove(srcPath1);
    remove(srcPath2);

    TEST_ASSERT_TRUE_MESSAGE(valid,
        "baseline (c1541-formatted + c1541-written two files) did not validate clean - "
        "cannot draw any conclusion from checking it");
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message.c_str());
}

// Tier 0: format() must produce a structurally valid blank image for all
// five in-scope formats (D64/D71/D80/D81/D82). Per the design spec, engine
// bugs found here are recorded in the findings file and NOT fixed - a
// failing format gets a TEST_IGNORE_MESSAGE naming its finding so the suite
// stays runnable, with the test body left intact so it goes live the moment
// that finding is fixed.
// ---------------------------------------------------------------------------
// Per-format test driving
// ---------------------------------------------------------------------------
//
// Each tier test runs against ONE format, selected by g_format_index, and
// process() runs the set once per format. The obvious alternative - looping
// over all_formats() inside each test - silently hides formats: Unity's assert
// macros longjmp out of the whole test function, so the first format to fail
// stops the rest from ever being exercised. A regression in d82 would sit
// behind a d64 failure and look as though it had been covered. Failure
// messages all lead with the format name, so the repeated Unity test name is
// not ambiguous.
static size_t g_format_index = 0;
static const FormatFixture& current_format() { return all_formats()[g_format_index]; }

void test_tier0_format_all_media(void)
{
    // finding #2 (shared D64MStream base - see the findings file) makes d64,
    // the first format in the table, fail check_invariants() before this
    // loop ever reaches d71/d80/d81/d82: TEST_FAIL_MESSAGE longjmps out of
    // the whole function on the first failing iteration, so it cannot report
    // per-format results on its own. All five formats' actual per-format
    // results (confirmed independently, outside this test, via a throwaway
    // diagnostic that isolates each format so a longjmp in one can't hide
    // the others) are recorded in
    // docs/superpowers/findings/2026-08-05-disk-write-findings.md: all five
    // fail check_invariants() the same way (header/directory-track sectors
    // in a chain but marked free in BAM); d64/d71/d81 additionally fail
    // c1541 validate, while d80/d82's c1541 check reports OK only because
    // c1541 prints "ERR = 65, NO BLOCK" for those formats' inconsistency
    // (verified with a direct c1541 -validate run) and c1541_validate()'s
    // text scan only recognizes "error"/"Error"/"wrong" - a gap in the
    // oracle helper, not evidence the d80/d82 images are actually fine.
    // The loop below is left intact, unmodified from the brief, so it goes
    // live the moment finding #2 is fixed.

    {
        const FormatFixture& f = current_format();
        std::string path = std::string("build_t0_") + f.name + "." + f.ext;
        remove(path.c_str());
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            char msg[128];
            snprintf(msg, sizeof(msg), "%s: formatImage failed", f.name);
            TEST_ASSERT_TRUE_MESSAGE(image->formatImage("testdisk", "01"), msg);

            InvariantResult r = check_invariants(*image);
            if (!r.ok)
            {
                std::string m = std::string(f.name) + ": " + r.message;
                TEST_FAIL_MESSAGE(m.c_str());
            }
        }
        if (f.has_c1541_oracle && c1541_available())
        {
            std::string m = std::string(f.name) + ": c1541 validate rejected the blank image";
            TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path), m.c_str());
        }
        remove(path.c_str());
    }
}

// The declared default size must agree with what the geometry tables imply.
// A mismatch means one of the two is wrong; without this check a bad table
// would silently validate against itself.
void test_tier0_declared_size_matches_geometry(void)
{
    // finding #3 (D71-specific, see the findings file): D71MStream::speedZone()
    // (lib/meatloaf/media/disk/d71.h) tests `track < 35` instead of
    // `track <= 35`, so track 35 falls through to the side-2 branch and gets
    // 21 sectors (sectorsPerTrack[3]) instead of the correct 17
    // (sectorsPerTrack[0]) - exactly 4 extra blocks, matching the observed
    // mismatch (350720 vs declared 349696 = 1370 vs 1366 blocks). d64 passes
    // this check, but d71 (the next format in the table) fails and
    // TEST_ASSERT_EQUAL_UINT32_MESSAGE longjmps out of the whole function
    // before d80/d81/d82 are ever reached. Confirmed independently (outside
    // this test, via a throwaway per-format diagnostic) that d64/d80/d81/d82
    // all match their declared size - only d71 is affected. The loop below

    {
        const FormatFixture& f = current_format();
        std::string path = std::string("build_t0g_") + f.name + "." + f.ext;
        remove(path.c_str());
        auto src = std::make_shared<FileContainerStream>(path, f.size);
        auto image = f.make(src);

        uint8_t last = image->partitions[image->partition]
                            .block_allocation_map.back().end_track;
        uint32_t blocks = 0;
        for (uint8_t t = 1; t <= last; t++)
            blocks += image->getSectorCount(t);

        char msg[192];
        snprintf(msg, sizeof(msg),
                 "%s: declared %u bytes but geometry implies %u (%u blocks)",
                 f.name, image->defaultImageSize(), blocks * 256, blocks);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(image->defaultImageSize(), blocks * 256, msg);
        remove(path.c_str());
    }
}

// ---------------------------------------------------------------------------
// Tier 1 - single-file write
// ---------------------------------------------------------------------------

// Drives the real SAVE path rather than the protected write primitives:
// getSourceStream() sets `mode` to out, seekPath() creates the entry and claims
// the first block, write() streams the data, and close() commits the directory
// entry via finalizeFileWrite(). Returns false if any step fails.
// CBM DOS status codes, from include/cbm_defines.h. Declared locally so this
// file does not have to pull in that header (it defines an ERROR macro that
// collides with <windows.h> on this host).
static const uint8_t CBM_ERR_WRITE_VERIFY = 25;  // ST_WRITE_VERIFY
static const uint8_t CBM_ERR_DIR_ERROR    = 71;  // ST_DIR_ERROR
static const uint8_t CBM_ERR_DISK_FULL    = 72;  // ST_DISK_FULL

// Returns 0 on success, otherwise the CBM error code the write failed with.
//
// Note where failures surface: close() runs finalizeFileWrite(), which is what
// commits the directory entry, so "no room left in the directory" is only
// discovered THERE - not at seekPath() or write(). This mirrors the drive,
// where iecChannelHandlerFile's destructor maps m_stream->error() to the drive
// status after the channel closes.
static uint8_t save_file_status(D64MStream& image,
                                const std::string& cbm_name,
                                const std::vector<uint8_t>& data)
{
    image.mode = std::ios_base::out;
    if (!image.seekPath(cbm_name))
        return image.error() ? (uint8_t)image.error() : CBM_ERR_WRITE_VERIFY;
    if (image.write(data.data(), (uint32_t)data.size()) != (uint32_t)data.size())
        return image.error() ? (uint8_t)image.error() : CBM_ERR_WRITE_VERIFY;
    image.close();
    return (uint8_t)image.error();
}

static bool save_file(D64MStream& image,
                      const std::string& cbm_name,
                      const std::vector<uint8_t>& data)
{
    return save_file_status(image, cbm_name, data) == 0;
}

void test_tier1_single_file_write(void)
{
    // A payload that spans more than one block (254 usable bytes each), so the
    // T/S chain is actually exercised rather than a single-block special case.
    std::vector<uint8_t> payload;
    for (int i = 0; i < 500; i++)
        payload.push_back((uint8_t)(i & 0xFF));

    {
        const FormatFixture& f = current_format();
        std::string path = std::string("build_t1_") + f.name + "." + f.ext;
        remove(path.c_str());

        uint16_t free_before = 0, free_after = 0;
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            char msg[128];
            snprintf(msg, sizeof(msg), "%s: formatImage failed", f.name);
            TEST_ASSERT_TRUE_MESSAGE(image->formatImage("testdisk", "01"), msg);
            free_before = image->blocksFree();

            snprintf(msg, sizeof(msg), "%s: save_file failed", f.name);
            TEST_ASSERT_TRUE_MESSAGE(save_file(*image, "hello", payload), msg);
        }

        // Reopen so the checks run against what actually landed on disk, not
        // against in-memory state left over from the write.
        {
            auto src = std::make_shared<FileContainerStream>(path);
            auto image = f.make(src);
            free_after = image->blocksFree();

            InvariantResult r = check_invariants(*image);
            if (!r.ok)
            {
                std::string m = std::string(f.name) + " after write: " + r.message;
                remove(path.c_str());
                TEST_FAIL_MESSAGE(m.c_str());
            }

            // 500 bytes needs 2 blocks (254 + 246).
            char msg[160];
            snprintf(msg, sizeof(msg), "%s: expected 2 blocks consumed, got %d",
                     f.name, (int)(free_before - free_after));
            if (free_before - free_after != 2)
            {
                remove(path.c_str());
                TEST_FAIL_MESSAGE(msg);
            }
        }

        if (f.has_c1541_oracle && c1541_available())
        {
            char msg[160];
            snprintf(msg, sizeof(msg), "%s: c1541 validate rejected the image after write", f.name);
            bool ok = c1541_validate(path);
            if (!ok) { remove(path.c_str()); TEST_FAIL_MESSAGE(msg); }

            // c1541 renders CBM (PETSCII) filenames in lower case, so compare
            // case-insensitively rather than assuming the shifted form.
            std::string listing = c1541_dir(path);
            std::string lower;
            for (char c : listing) lower += (char)tolower((unsigned char)c);
            if (lower.find("\"hello\"") == std::string::npos)
            {
                std::string tail = listing.size() > 120
                                 ? listing.substr(listing.size() - 120) : listing;
                snprintf(msg, sizeof(msg), "%s: hello missing; listing tail >>>%s<<<",
                         f.name, tail.c_str());
                remove(path.c_str());
                TEST_FAIL_MESSAGE(msg);
            }

            std::string out = path + ".out";
            size_t got_bytes = 0;
            bool read_ok = c1541_read(path, "hello", out, &got_bytes);
            if (!read_ok)
            {
                snprintf(msg, sizeof(msg), "%s: c1541 could not read HELLO back", f.name);
                remove(path.c_str()); remove(out.c_str());
                TEST_FAIL_MESSAGE(msg);
            }

            std::vector<uint8_t> got(payload.size(), 0);
            FILE* fp = fopen(out.c_str(), "rb");
            size_t n = fp ? fread(got.data(), 1, got.size(), fp) : 0;
            if (fp) fclose(fp);
            remove(out.c_str());

            if (n != payload.size() || got != payload)
            {
                snprintf(msg, sizeof(msg),
                         "%s: read-back mismatch (%u of %u bytes matched size)",
                         f.name, (unsigned)n, (unsigned)payload.size());
                remove(path.c_str());
                TEST_FAIL_MESSAGE(msg);
            }
        }

        remove(path.c_str());
    }
}

// ---------------------------------------------------------------------------
// Tier 2 - structural stress
// ---------------------------------------------------------------------------

// Opens a fresh stream over an existing image and runs both validators.
// Reopening matters: it checks what is on disk, not leftover in-memory state.
static void assert_image_sound(const FormatFixture& f,
                               const std::string& path,
                               const char* stage)
{
    {
        auto src = std::make_shared<FileContainerStream>(path);
        auto image = f.make(src);
        InvariantResult r = check_invariants(*image);
        if (!r.ok)
        {
            std::string m = std::string(f.name) + " " + stage + ": " + r.message;
            remove(path.c_str());
            TEST_FAIL_MESSAGE(m.c_str());
        }
    }
    if (f.has_c1541_oracle && c1541_available() && !c1541_validate(path))
    {
        std::string m = std::string(f.name) + " " + stage + ": c1541 validate rejected the image";
        remove(path.c_str());
        TEST_FAIL_MESSAGE(m.c_str());
    }
}

// Reads blocksFree() from a freshly opened stream. Reading it from a stream
// that has just been close()d returns 0 - close() drops state blocksFree()
// needs, and the next seekPath() rebuilds it - so every free-block reading in
// these tests comes from a reopen.
static uint16_t blocks_free_of(const FormatFixture& f, const std::string& path)
{
    auto src = std::make_shared<FileContainerStream>(path);
    auto image = f.make(src);
    return image->blocksFree();
}

// Opens an existing image, saves one file into it, and closes.
static bool open_and_save(const FormatFixture& f,
                          const std::string& path,
                          const std::string& name,
                          const std::vector<uint8_t>& data,
                          bool allow_grow = false)
{
    auto src = std::make_shared<FileContainerStream>(path);
    auto image = f.make(src);
    // Growing the medium is opt-in and off by default, matching the engine.
    image->allow_grow = allow_grow;
    return save_file(*image, name, data);
}

// Saves files of a fixed size until one fails. Returns how many succeeded and,
// via out_status, the CBM error the failing save reported.
//
// Every save gets a FRESH stream, and that is not incidental: after close() the
// stream's BAM state is gone (blocksFree() reads 0) and the very next write on
// the same stream fails with DISK FULL. This mirrors the drive, where each SAVE
// opens its own channel. Looping saves on one stream instead makes every save
// after the first fail at finalizeFileWrite() - silently, if the caller only
// looks at a bool that ignores close().
static int fill_until_full(const FormatFixture& f,
                           const std::string& path,
                           size_t payload_size,
                           int limit,
                           uint8_t* out_status = nullptr)
{
    std::vector<uint8_t> payload(payload_size, 0xAA);
    int n = 0;
    uint8_t st = 0;
    while (n < limit)
    {
        char name[24];
        snprintf(name, sizeof(name), "file%d", n);

        auto src = std::make_shared<FileContainerStream>(path);
        auto image = f.make(src);
        st = save_file_status(*image, name, payload);
        if (st != 0) break;
        n++;
    }
    if (out_status) *out_status = st;
    return n;
}

void test_tier2_multiblock_crossing_tracks(void)
{
    // 30 blocks cannot fit on one track of any of these formats, so the chain
    // is forced across a track boundary and through the interleave logic.
    std::vector<uint8_t> payload(254 * 30, 0x5A);

    {
        const FormatFixture& f = current_format();
        std::string path = std::string("build_t2a_") + f.name + "." + f.ext;
        remove(path.c_str());
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));
            char msg[128];
            snprintf(msg, sizeof(msg), "%s: multi-track save failed", f.name);
            TEST_ASSERT_TRUE_MESSAGE(save_file(*image, "big", payload), msg);
        }
        assert_image_sound(f, path, "after multi-track write");
        remove(path.c_str());
    }
}

void test_tier2_disk_full_rolls_back(void)
{
    {
        const FormatFixture& f = current_format();
        // DNP never reaches DISK FULL by design - a native partition grows a
        // track at a time instead. Its ceiling is 255 tracks (~65280 blocks),
        // far too many to fill in a test. Growth is covered separately by
        // test_dnp_grows_beyond_its_initial_track.
        if (std::string(f.name) == "dnp")
            TEST_IGNORE_MESSAGE("dnp: scenario not applicable - see comment above");

        std::string path = std::string("build_t2b_") + f.name + "." + f.ext;
        remove(path.c_str());
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));
        }

        uint8_t status = 0;
        int saved = fill_until_full(f, path, 254 * 10, 5000, &status);
        char msg[160];
        snprintf(msg, sizeof(msg), "%s: nothing saved before the disk filled", f.name);
        TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, saved, msg);
        snprintf(msg, sizeof(msg), "%s: expected DISK FULL (%u) after %d files, got %u",
                 f.name, (unsigned)CBM_ERR_DISK_FULL, saved, (unsigned)status);
        if (status != CBM_ERR_DISK_FULL) { remove(path.c_str()); TEST_FAIL_MESSAGE(msg); }
        // The save that failed must leave NO half-allocated blocks behind.
        // Invariant 2 (allocated but unreachable) is what catches a broken
        // rollback, which is why this stage runs the full checker.
        assert_image_sound(f, path, "after disk full");
        remove(path.c_str());
    }
}

void test_tier2_directory_extension(void)
{
    {
        const FormatFixture& f = current_format();
        std::string path = std::string("build_t2c_") + f.name + "." + f.ext;
        remove(path.c_str());
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));
        }

        // 8 entries fit per directory sector, so 40 tiny files force the
        // directory to extend onto several sectors.
        int saved = fill_until_full(f, path, 16, 40);
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: only %d files saved, directory never extended",
                 f.name, saved);
        TEST_ASSERT_GREATER_THAN_INT_MESSAGE(8, saved, msg);
        assert_image_sound(f, path, "after directory extension");
        remove(path.c_str());
    }
}

void test_tier2_overwrite_reuses_slot(void)
{
    std::vector<uint8_t> first(254 * 4, 0x11);
    std::vector<uint8_t> second(254 * 2, 0x22);

    {
        const FormatFixture& f = current_format();
        std::string path = std::string("build_t2d_") + f.name + "." + f.ext;
        remove(path.c_str());
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));
        }

        char msg[192];
        snprintf(msg, sizeof(msg), "%s: first save failed", f.name);
        TEST_ASSERT_TRUE_MESSAGE(open_and_save(f, path, "doc", first), msg);
        uint16_t free_after_first = blocks_free_of(f, path);

        // Save over the same name. Note the CBM "@:" save-and-replace prefix is
        // stripped by the DRIVE layer (drive.cpp sets an overwrite flag and
        // passes the bare name down) - the engine never sees it. seekPath()
        // decides on its own: a name that resolves to an existing file is
        // scratched and its slot reused. Passing a literal "@:doc" here would
        // just create a second file called "@:doc".
        // The old 4-block chain must be returned, not leaked.
        snprintf(msg, sizeof(msg), "%s: overwrite save failed", f.name);
        TEST_ASSERT_TRUE_MESSAGE(open_and_save(f, path, "doc", second), msg);
        uint16_t free_after_overwrite = blocks_free_of(f, path);

        // Structural soundness first: if the old chain was dropped without being
        // deallocated, its blocks are allocated-but-unreachable and invariant 2
        // reports them as orphans. That distinguishes real corruption from a
        // mere free-count discrepancy.
        assert_image_sound(f, path, "after @: overwrite");

        snprintf(msg, sizeof(msg),
                 "%s: overwrite did not release the old chain (free %u -> %u, expected +2)",
                 f.name, (unsigned)free_after_first, (unsigned)free_after_overwrite);
        if (free_after_overwrite != free_after_first + 2)
        {
            remove(path.c_str());
            TEST_FAIL_MESSAGE(msg);
        }
        remove(path.c_str());
    }
}

void test_tier2_directory_full_reports_disk_full(void)
{
    // One block per file, so the DIRECTORY runs out long before the disk does.
    // A D64's directory track holds 18 sectors x 8 entries = 144 files against
    // 664 free blocks; every other format has a similar margin. That isolates
    // "directory full" from "disk full" - CBM DOS reports both as 72, so the
    // free-block assertion below is what proves which one we actually hit.
    {
        const FormatFixture& f = current_format();
        // DNP cannot reach this state. Its directory lives on track 1 and can
        // extend across ~220 sectors = ~1760 entries, but every entry also
        // consumes a data block and the whole partition only has 1024 - so the
        // DISK fills first, no matter the size. Sizing it up to change that
        // makes the exhaustive per-operation checks prohibitively slow.
        if (std::string(f.name) == "dnp")
            TEST_IGNORE_MESSAGE("dnp: scenario not applicable - see comment above");

        std::string path = std::string("build_t2f_") + f.name + "." + f.ext;
        remove(path.c_str());
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));
        }

        uint8_t status = 0;
        int saved = fill_until_full(f, path, 16, 4000, &status);
        uint16_t free_at_failure = blocks_free_of(f, path);

        char msg[224];

        // It has to actually run out, not just stop early.
        snprintf(msg, sizeof(msg),
                 "%s: saved %d files without ever failing - directory never filled",
                 f.name, saved);
        if (status == 0) { remove(path.c_str()); TEST_FAIL_MESSAGE(msg); }

        // More than one directory sector's worth, so the directory genuinely
        // extended before it ran out.
        snprintf(msg, sizeof(msg),
                 "%s: only %d files fit, directory never extended past its first sector",
                 f.name, saved);
        if (saved <= 8) { remove(path.c_str()); TEST_FAIL_MESSAGE(msg); }

        snprintf(msg, sizeof(msg),
                 "%s: expected error %u (DISK FULL) after %d files, got %u",
                 f.name, (unsigned)CBM_ERR_DISK_FULL, saved, (unsigned)status);
        if (status != CBM_ERR_DISK_FULL) { remove(path.c_str()); TEST_FAIL_MESSAGE(msg); }

        // Blocks still free => the directory ran out, not the disk. Without
        // this the test would pass just as happily on a genuinely full disk.
        snprintf(msg, sizeof(msg),
                 "%s: no blocks left at failure (%u), so this was disk-full not directory-full",
                 f.name, (unsigned)free_at_failure);
        if (free_at_failure == 0) { remove(path.c_str()); TEST_FAIL_MESSAGE(msg); }

        // The rejected save must have rolled back cleanly - no half-written
        // entry, no blocks allocated to a file that does not exist.
        assert_image_sound(f, path, "after directory full");
        remove(path.c_str());
    }
}

void test_tier2_bam_record_boundary(void)
{
    // d64 and d81 have a single BAM record; the others span more than one and
    // are where record-boundary arithmetic can go wrong. d71's side-2 record is
    // bitmap-only (no leading free count), d80/d82 carry several counted
    // records, so filling most of the disk walks across those boundaries.
    {
        const FormatFixture& f = current_format();
        // d64/d81 have a single BAM record, and so does DNP - there is no
        // record boundary to cross on any of them.
        if (std::string(f.name) == "d64" || std::string(f.name) == "d81" ||
            std::string(f.name) == "dnp")
            TEST_IGNORE_MESSAGE("single BAM record - no boundary to cross");

        std::string path = std::string("build_t2e_") + f.name + "." + f.ext;
        remove(path.c_str());
        {
            auto src = std::make_shared<FileContainerStream>(path, f.size);
            auto image = f.make(src);
            TEST_ASSERT_TRUE(image->formatImage("testdisk", "01"));
        }
        fill_until_full(f, path, 254 * 20, 5000);
        assert_image_sound(f, path, "after crossing BAM records");
        remove(path.c_str());
    }
}

// ---------------------------------------------------------------------------
// Tier 3 - seeded randomized stress
// ---------------------------------------------------------------------------

// Issues a deterministic pseudo-random sequence of saves over a small pool of
// reused names, checking structure after EVERY operation so a failure names the
// exact op that broke the image rather than handing back an end state to
// bisect. Reusing names means many operations are overwrites, which exercises
// scratch-and-reallocate interleaved with fresh allocation - the sequences
// hand-written scenarios do not reach.
static void run_random_session(const FormatFixture& f, unsigned seed)
{
    std::string path = std::string("build_t3_") + f.name + "_" +
                       std::to_string(seed) + "." + f.ext;
    remove(path.c_str());
    {
        auto src = std::make_shared<FileContainerStream>(path, f.size);
        auto image = f.make(src);
        char msg[128];
        snprintf(msg, sizeof(msg), "%s seed=%u: formatImage failed", f.name, seed);
        TEST_ASSERT_TRUE_MESSAGE(image->formatImage("stress", "01"), msg);
    }

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> size_dist(1, 254 * 6);
    std::uniform_int_distribution<int> name_dist(0, 7);

    for (int op = 0; op < 25; op++)
    {
        char name[24];
        snprintf(name, sizeof(name), "f%d", name_dist(rng));
        std::vector<uint8_t> payload((size_t)size_dist(rng), (uint8_t)(op & 0xFF));

        // A failed save is legitimate here - the disk fills up. What must hold
        // either way is that the image is still structurally sound afterwards,
        // i.e. a failure rolled back cleanly.
        open_and_save(f, path, name, payload);

        {
            auto src = std::make_shared<FileContainerStream>(path);
            auto image = f.make(src);
            InvariantResult r = check_invariants(*image);
            if (!r.ok)
            {
                char m[256];
                snprintf(m, sizeof(m), "%s seed=%u op=%d (%s, %u bytes): %s",
                         f.name, seed, op, name, (unsigned)payload.size(), r.message.c_str());
                remove(path.c_str());
                TEST_FAIL_MESSAGE(m);
            }
        }
    }

    if (f.has_c1541_oracle && c1541_available() && !c1541_validate(path))
    {
        char m[160];
        snprintf(m, sizeof(m), "%s seed=%u: c1541 validate rejected the image after the session",
                 f.name, seed);
        remove(path.c_str());
        TEST_FAIL_MESSAGE(m);
    }
    remove(path.c_str());
}

// Growing is a MEATLOAF EXTENSION, opt-in via allow_grow. With it set, a
// partition created as a single 64 KB track must extend rather than fail when
// more than a track of data is written - and stay structurally sound.
// test_dnp_does_not_grow_by_default covers the other half: that the default is
// still fixed-size CMD behaviour.
void test_dnp_grows_beyond_its_initial_track(void)
{
    const FormatFixture* dnp = nullptr;
    for (const auto& f : all_formats())
        if (std::string(f.name) == "dnp") { dnp = &f; break; }
    TEST_ASSERT_NOT_NULL_MESSAGE(dnp, "dnp fixture missing from all_formats()");

    const char* path = "build_dnp_grow.dnp";
    remove(path);
    {
        auto src = std::make_shared<FileContainerStream>(path, dnp->size);
        auto image = dnp->make(src);
        TEST_ASSERT_TRUE_MESSAGE(image->formatImage("grow", "01"), "dnp: formatImage failed");
    }

    // One track is 256 blocks of 254 usable bytes. Write appreciably more than
    // that across several files, so growth is forced more than once.
    std::vector<uint8_t> chunk(254 * 100, 0x5A);
    for (int i = 0; i < 5; i++)
    {
        char name[24];
        snprintf(name, sizeof(name), "grow%d", i);
        char msg[128];
        snprintf(msg, sizeof(msg), "dnp: save %d failed - partition did not grow", i);
        if (!open_and_save(*dnp, path, name, chunk, /*allow_grow=*/true))
        {
            remove(path);
            TEST_FAIL_MESSAGE(msg);
        }
    }

    // The container must actually be bigger than the single track it started as.
    FILE* fp = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    fseek(fp, 0, SEEK_END);
    long grown = ftell(fp);
    fclose(fp);

    char msg[160];
    snprintf(msg, sizeof(msg),
             "dnp: expected the image to grow past %u bytes, it is %ld",
             (unsigned)dnp->size, grown);
    if (grown <= (long)dnp->size) { remove(path); TEST_FAIL_MESSAGE(msg); }

    // Growth must be a whole number of 64 KB tracks.
    snprintf(msg, sizeof(msg), "dnp: grown size %ld is not a whole number of 64 KB tracks", grown);
    if (grown % 65536 != 0) { remove(path); TEST_FAIL_MESSAGE(msg); }

    assert_image_sound(*dnp, path, "after growing");
    remove(path);
}

// The default must remain fixed-size, matching real CMD behaviour. This also
// guards the case that actually matters for corruption: a DNP embedded in a DHD
// occupies a fixed window, and a partition that grew there would write straight
// over its neighbour.
void test_dnp_does_not_grow_by_default(void)
{
    const FormatFixture* dnp = nullptr;
    for (const auto& f : all_formats())
        if (std::string(f.name) == "dnp") { dnp = &f; break; }
    TEST_ASSERT_NOT_NULL_MESSAGE(dnp, "dnp fixture missing from all_formats()");

    const char* path = "build_dnp_nogrow.dnp";
    remove(path);
    {
        auto src = std::make_shared<FileContainerStream>(path, dnp->size);
        auto image = dnp->make(src);
        TEST_ASSERT_TRUE_MESSAGE(image->formatImage("nogrow", "01"), "dnp: formatImage failed");
    }

    // A single track holds 221 free blocks after the system area. Writing well
    // past that must fail rather than extend the container.
    std::vector<uint8_t> chunk(254 * 100, 0x5A);
    uint8_t status = 0;
    int saved = 0;
    for (; saved < 5; saved++)
    {
        auto src = std::make_shared<FileContainerStream>(path);
        auto image = dnp->make(src);          // allow_grow stays false
        status = save_file_status(*image, "f" + std::to_string(saved), chunk);
        if (status != 0) break;
    }

    char msg[192];
    snprintf(msg, sizeof(msg),
             "dnp: wrote %d x 100 blocks into a 1-track partition without failing - it grew", saved);
    if (status == 0) { remove(path); TEST_FAIL_MESSAGE(msg); }

    snprintf(msg, sizeof(msg), "dnp: expected DISK FULL (%u), got %u",
             (unsigned)CBM_ERR_DISK_FULL, (unsigned)status);
    if (status != CBM_ERR_DISK_FULL) { remove(path); TEST_FAIL_MESSAGE(msg); }

    // The container must be exactly the size it was created with.
    FILE* fp = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    fseek(fp, 0, SEEK_END);
    long size_now = ftell(fp);
    fclose(fp);

    snprintf(msg, sizeof(msg), "dnp: container grew from %u to %ld bytes with allow_grow off",
             (unsigned)dnp->size, size_now);
    if (size_now != (long)dnp->size) { remove(path); TEST_FAIL_MESSAGE(msg); }

    assert_image_sound(*dnp, path, "after filling without growth");
    remove(path);
}

// track_count selects the geometry at format time; 0 means the media default.
// error_info appends one status byte per sector after the data area. The
// expected sizes below are the canonical ones the D64MStream constructor
// already recognises in its size switch, so this test and the reader agree.
struct GeometryCase
{
    const char* name;
    std::shared_ptr<D64MStream> (*make)(std::shared_ptr<MStream>);
    size_t      tracks;       // 0 = media default
    bool        error_info;
    uint32_t    expected_size;
    const char* why;
};

void test_format_honours_track_count_and_error_info(void)
{
    const GeometryCase cases[] = {
        { "d64", make_d64,  0, false, 174848, "default = 35 tracks" },
        { "d64", make_d64, 35, false, 174848, "35 tracks explicitly" },
        { "d64", make_d64, 40, false, 196608, "40-track D64" },
        { "d64", make_d64, 42, false, 205312, "42-track D64" },
        { "d64", make_d64, 35, true,  175531, "35 tracks + 683 error bytes" },
        { "d64", make_d64, 40, true,  197376, "40 tracks + 768 error bytes" },
        { "d64", make_d64, 42, true,  206114, "42 tracks + 802 error bytes" },
        { "d71", make_d71,  0, false, 349696, "default = 70 tracks" },
        { "d81", make_d81,  0, false, 819200, "default = 80 tracks" },
        { "d81", make_d81, 81, false, 829440, "81-track D81" },
        { "dnp", make_dnp,  0, false,  65536, "default = 1 track" },
        { "dnp", make_dnp,  4, false, 262144, "DNP created with 4 tracks" },
        { "dnp", make_dnp, 16, false,1048576, "DNP created with 16 tracks" },
    };

    for (const auto& c : cases)
    {
        std::string path = std::string("build_geom_") + c.name + "_" +
                           std::to_string(c.tracks) + (c.error_info ? "e" : "") + ".img";
        remove(path.c_str());
        {
            // Start from an EMPTY container so formatImage() must size it
            // itself rather than inherit a size the test chose. Note
            // FileContainerStream(path, 0) opens an EXISTING file, so the file
            // has to be created first - it just has no bytes in it.
            { FILE* mk = fopen(path.c_str(), "wb"); if (mk) fclose(mk); }
            auto src = std::make_shared<FileContainerStream>(path, 0);
            auto image = c.make(src);
            char msg[160];
            snprintf(msg, sizeof(msg), "%s (%s): formatImage failed", c.name, c.why);
            TEST_ASSERT_TRUE_MESSAGE(image->formatImage("geom", "01", c.tracks, c.error_info), msg);
        }

        FILE* fp = fopen(path.c_str(), "rb");
        TEST_ASSERT_NOT_NULL(fp);
        fseek(fp, 0, SEEK_END);
        long got = ftell(fp);
        fclose(fp);

        char msg[192];
        snprintf(msg, sizeof(msg), "%s (%s): expected %u bytes, got %ld",
                 c.name, c.why, (unsigned)c.expected_size, got);
        if (got != (long)c.expected_size) { remove(path.c_str()); TEST_FAIL_MESSAGE(msg); }
        remove(path.c_str());
    }
}

// The N0: argument is parsed from a string the C64 sends, so it has to cope
// with anything. Bad trailing fields must fall back to the defaults rather
// than produce a nonsense image - and must not throw, since ESP-IDF builds
// with -fno-exceptions.
void test_parse_format_spec(void)
{
    struct Case {
        const char* in;
        const char* name;
        const char* id;
        size_t      tracks;
        bool        err;
        const char* why;
    };

    const Case cases[] = {
        { "disk,01",        "disk", "01",  0, false, "plain CBM name,id" },
        { "disk",           "disk", "",    0, false, "no id at all" },
        { "",               "",     "",    0, false, "empty string" },
        { "disk,01,40",     "disk", "01", 40, false, "explicit track count" },
        { "disk,01,40,1",   "disk", "01", 40, true,  "track count + error info" },
        { "disk,01,,1",     "disk", "01",  0, true,  "default geometry + error info" },
        { "disk,01,0",      "disk", "01",  0, false, "zero tracks means default" },
        { "disk,01,256",    "disk", "01",  0, false, "out of range - falls back" },
        { "disk,01,-5",     "disk", "01",  0, false, "negative - falls back" },
        { "disk,01,abc",    "disk", "01",  0, false, "not a number - falls back" },
        { "disk,01,40x",    "disk", "01",  0, false, "trailing junk - falls back" },
        { "disk,01,40,0",   "disk", "01", 40, false, "explicit error_info off" },
        { "disk,01,40,yes", "disk", "01", 40, false, "non-numeric flag - falls back" },
        { "disk,01,40,1,9", "disk", "01", 40, true,  "extra field ignored" },
    };

    for (const auto& c : cases)
    {
        D64FormatSpec got = parseD64FormatSpec(c.in);
        char msg[192];

        snprintf(msg, sizeof(msg), "\"%s\" (%s): name expected \"%s\", got \"%s\"",
                 c.in, c.why, c.name, got.name.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.name, got.name.c_str(), msg);

        snprintf(msg, sizeof(msg), "\"%s\" (%s): id expected \"%s\", got \"%s\"",
                 c.in, c.why, c.id, got.id.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.id, got.id.c_str(), msg);

        snprintf(msg, sizeof(msg), "\"%s\" (%s): tracks expected %u, got %u",
                 c.in, c.why, (unsigned)c.tracks, (unsigned)got.track_count);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)c.tracks, (uint32_t)got.track_count, msg);

        snprintf(msg, sizeof(msg), "\"%s\" (%s): error_info expected %d, got %d",
                 c.in, c.why, (int)c.err, (int)got.error_info);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)c.err, (int)got.error_info, msg);
    }
}

void test_tier3_randomized_stress(void)
{
    // Fixed seeds keep failures reproducible. When a seed finds a bug, keep it
    // in this list permanently - it becomes a regression case for free.
    const unsigned seeds[] = { 1, 42, 1337 };
    const FormatFixture& f = current_format();
    for (unsigned s : seeds)
        run_random_session(f, s);
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
    RUN_TEST(test_c1541_validate_detects_cbm_error_channel_report);
    RUN_TEST(test_invariants_pass_on_clean_c1541_image_with_two_files);
    RUN_TEST(test_invariants_pass_on_blank_image);
    RUN_TEST(test_dnp_grows_beyond_its_initial_track);
    RUN_TEST(test_dnp_does_not_grow_by_default);
    RUN_TEST(test_format_honours_track_count_and_error_info);
    RUN_TEST(test_parse_format_spec);
    // Once per format, so no format can be hidden behind another's failure.
    for (g_format_index = 0; g_format_index < all_formats().size(); g_format_index++)
    {
        RUN_TEST(test_tier0_format_all_media);
        RUN_TEST(test_tier0_declared_size_matches_geometry);
        RUN_TEST(test_tier1_single_file_write);
        RUN_TEST(test_tier2_multiblock_crossing_tracks);
        RUN_TEST(test_tier2_disk_full_rolls_back);
        RUN_TEST(test_tier2_directory_extension);
        RUN_TEST(test_tier2_overwrite_reuses_slot);
        RUN_TEST(test_tier2_directory_full_reports_disk_full);
        RUN_TEST(test_tier2_bam_record_boundary);
        RUN_TEST(test_tier3_randomized_stress);
    }

    UNITY_END();
}

int main(int argc, char **argv)
{
    process();
    return 0;
}
