// Read-path tests for the P64 flux-level disk format.
//
// A P64 carries no sectors and no GCR bytes - it carries the magnetic flux
// transitions of each half track, range-coded. Getting one CBM sector out of it
// is three decodes stacked, and each is silent about the next being wrong:
//
//   range-coded chunk  ->  flux pulses  ->  GCR bitstream  ->  CBM sector
//
// So these tests do not assert on intermediate shapes. They assert on the one
// thing that cannot come out right by accident: real CBM DOS structures. A
// blank disk's BAM has an exactly predictable free count for every track in
// every speed zone, and a real release's directory has exact filenames at exact
// offsets. Getting those back proves the chunk walk, the range decoder, the
// pulse-to-GCR read logic, the bit-resolution sync search and the GCR nibble
// decode all agree, end to end.
//
// The images are the real ones in .data/media/p64 rather than synthesized: nothing
// here can produce a P64, since writing one means implementing the range
// ENCODER, which the read path neither has nor needs. .data/media is gitignored,
// so every test skips cleanly without it.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/disk/p64.h"

static const char* CORPUS_DIR = ".data/media/p64";

// A freshly formatted, empty 1541 disk - no disk name, no files. Every BAM
// field in it is predictable from the geometry alone.
static const char* BLANK = "test.p64";

// Wheels 4.4a, a real GEOS release: a named disk with a populated directory.
static const char* WHEELS = "wheels64_4.4a.p64";

// Every P64 in the corpus. The two commercial titles carry protection.
static const char* CORPUS[] = {
    "test.p64", "wheels64_4.4a.p64", "zetawingii.p64",
};

// readHeader()/parseChunks()/half_tracks are protected; the tests drive them
// the way the directory listing and the drive do.
class TestP64Stream : public P64MStream
{
public:
    using P64MStream::P64MStream;
    using P64MStream::decodeTrack;
    using P64MStream::gcr_track_bytes;
    using P64MStream::half_tracks;
    using P64MStream::loadSector;
    using P64MStream::parseChunks;
    using P64MStream::readHeader;
    using P64MStream::sector_buffer;
    using P64MStream::last_data_checksum_ok;
};

// Opens a corpus image, or returns nullptr when the corpus is not present.
// Every test builds its own stream over its own FileContainerStream - nothing
// is shared, because Unity's TEST_ASSERT failure path is a longjmp that does
// not unwind C++ destructors, so any state a failing test left behind would
// follow the tests after it.
static std::shared_ptr<TestP64Stream> openImage(const char* name,
                                                std::shared_ptr<FileContainerStream>& src)
{
    src = std::make_shared<FileContainerStream>(std::string(CORPUS_DIR) + "/" + name);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestP64Stream>(src);
    // mode is uninitialised on a directly constructed stream - only
    // MFile::getSourceStream() sets it, and garbage carrying the out bit sends
    // seekPath() down the write branch.
    image->mode = std::ios_base::in;
    return image;
}

void setUp(void) {}
void tearDown(void) {}


// ---------------------------------------------------------------------------
// The chunk stream
// ---------------------------------------------------------------------------

// The header identifies the file and the chunk walk finds the half tracks. A
// half track index is track * 2, so track 18 - where every CBM directory lives
// - is half track 36, and an image with no chunk 36 has nothing to list.
void test_chunk_walk_finds_half_tracks(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(BLANK, src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/p64");

    TEST_ASSERT_TRUE(image->parseChunks());
    TEST_ASSERT_TRUE(image->half_tracks.size() >= 35);
    TEST_ASSERT_EQUAL_UINT32(1, image->half_tracks.count(36));

    // Every entry names a real, non-empty run of range-coded bytes inside the
    // container.
    for (const auto& ht : image->half_tracks)
    {
        TEST_ASSERT_TRUE(ht.first >= 2 && ht.first <= 85);
        TEST_ASSERT_TRUE(ht.second.pulses > 0);
        TEST_ASSERT_TRUE(ht.second.size > 0);
        TEST_ASSERT_TRUE(ht.second.offset + ht.second.size <= src->size());
    }
}

// A second walk must not re-read the container: readHeader() runs on every
// directory rewind, and over the network each walk is a request per chunk.
void test_chunk_walk_is_idempotent(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(BLANK, src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/p64");

    TEST_ASSERT_TRUE(image->parseChunks());
    size_t first = image->half_tracks.size();

    TEST_ASSERT_TRUE(image->parseChunks());
    TEST_ASSERT_EQUAL_UINT32(first, image->half_tracks.size());
}

// Anything that is not a P64-1541 has to be refused rather than walked, since
// the chunk loop would otherwise wander through arbitrary bytes.
void test_non_p64_is_refused(void)
{
    const std::string path = "build_p64_not_a_p64.bin";

    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    const char junk[64] = "GCR-1541 this is not a P64 at all";
    fwrite(junk, 1, sizeof(junk), fp);
    fclose(fp);

    auto src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(src->isOpen());

    auto image = std::make_shared<TestP64Stream>(src);
    image->mode = std::ios_base::in;

    TEST_ASSERT_FALSE(image->parseChunks());
    TEST_ASSERT_FALSE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(0, image->half_tracks.size());

    src->close();
    remove(path.c_str());
}


// ---------------------------------------------------------------------------
// Decoding a track
// ---------------------------------------------------------------------------

// One rotation is 3200000 samples at 16 MHz and a bit cell is 4 * (16 - zone)
// of them, so track 18 (speed zone 2, 56 cycles per bit) has to come out at
// about 3200000 / 56 / 8 = 7143 bytes. A decode that stops early - a range
// coder that desynchronises, a pulse stream that ends - lands far short of
// that, which a plain "did it decode?" check would not notice.
void test_track_decodes_to_a_full_rotation(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(BLANK, src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/p64");

    TEST_ASSERT_TRUE(image->parseChunks());
    TEST_ASSERT_TRUE(image->decodeTrack(18));

    // One rotation plus the overlap that is decoded past its end.
    TEST_ASSERT_TRUE(image->gcr_track_bytes > 6800);
    TEST_ASSERT_TRUE(image->gcr_track_bytes <= 7143 + P64_OVERLAP_BYTES + 8);
}

// The BAM of a freshly formatted disk, which is the strongest cheap assertion
// available: 35 four-byte records whose free counts are fully determined by the
// speed zone geometry, with only the two blocks the format itself uses taken.
// A decode that is off anywhere lands somewhere other than exactly this.
void test_blank_disk_bam_is_exactly_right(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(BLANK, src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/p64");

    TEST_ASSERT_TRUE(image->parseChunks());
    TEST_ASSERT_TRUE(image->decodeTrack(18));
    TEST_ASSERT_TRUE(image->loadSector(18, 0));

    // Link to the first directory block, then the DOS version byte 'A'.
    TEST_ASSERT_EQUAL_UINT8(18, image->sector_buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(1, image->sector_buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x41, image->sector_buffer[2]);

    for (uint8_t track = 1; track <= 35; track++)
    {
        const uint8_t* record = image->sector_buffer + 4 + ((track - 1) * 4);

        uint8_t sectors = (track < 18) ? 21 : (track < 25) ? 19 : (track < 31) ? 18 : 17;
        // Track 18 holds the BAM block and the first directory block.
        uint8_t expected = (track == 18) ? (uint8_t)(sectors - 2) : sectors;

        char message[48];
        snprintf(message, sizeof(message), "BAM free count, track %u", (unsigned)track);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected, record[0], message);

        // The bitmap's set bits have to agree with the count byte.
        uint8_t free_bits = 0;
        for (uint8_t bit = 0; bit < sectors; bit++)
            if (record[1 + (bit >> 3)] & (1 << (bit & 7)))
                free_bits++;

        snprintf(message, sizeof(message), "BAM bitmap, track %u", (unsigned)track);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected, free_bits, message);
    }

    // 2A is the DOS format marker of every 1541 disk.
    TEST_ASSERT_EQUAL_UINT8('2', image->sector_buffer[0xa5]);
    TEST_ASSERT_EQUAL_UINT8('A', image->sector_buffer[0xa6]);

    // A blank disk's directory block is empty and ends the chain.
    TEST_ASSERT_TRUE(image->loadSector(18, 1));
    TEST_ASSERT_EQUAL_UINT8(0x00, image->sector_buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0xff, image->sector_buffer[1]);
    for (int i = 0; i < 8; i++)
        TEST_ASSERT_EQUAL_UINT8(0, image->sector_buffer[2 + (i * 32)]);
}

// Every sector of the directory track, not just the first: 19 independent
// sector headers, each with its own checksum, all found by scanning one decoded
// bitstream at bit resolution.
void test_every_sector_of_track18_is_readable(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(BLANK, src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/p64");

    TEST_ASSERT_TRUE(image->parseChunks());
    TEST_ASSERT_TRUE(image->decodeTrack(18));

    for (uint8_t sector = 0; sector < 19; sector++)
    {
        char message[48];
        snprintf(message, sizeof(message), "track 18 sector %u", (unsigned)sector);
        TEST_ASSERT_TRUE_MESSAGE(image->loadSector(18, sector), message);
    }
}

// seekSector() is the whole read path the D64 layer sits on: it validates the
// geometry, decodes the track, finds the sector and leaves readContainer()
// positioned at the requested offset within it.
void test_seek_sector_positions_read_container(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(BLANK, src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/p64");

    TEST_ASSERT_TRUE(image->parseChunks());

    // From the start of the sector.
    TEST_ASSERT_TRUE(image->seekSector(18, 0, 0));
    uint8_t link[2] = { 0, 0 };
    TEST_ASSERT_EQUAL_UINT32(2, image->readContainer(link, 2));
    TEST_ASSERT_EQUAL_UINT8(18, link[0]);
    TEST_ASSERT_EQUAL_UINT8(1, link[1]);

    // From the DOS marker offset, which only lands on '2A' if the offset was
    // applied to the sector rather than to the file position.
    TEST_ASSERT_TRUE(image->seekSector(18, 0, 0xa5));
    uint8_t marker[2] = { 0, 0 };
    TEST_ASSERT_EQUAL_UINT32(2, image->readContainer(marker, 2));
    TEST_ASSERT_EQUAL_UINT8('2', marker[0]);
    TEST_ASSERT_EQUAL_UINT8('A', marker[1]);

    // A read can never run past the end of the sector it is positioned in.
    TEST_ASSERT_TRUE(image->seekSector(18, 0, 250));
    uint8_t tail[64];
    TEST_ASSERT_EQUAL_UINT32(6, image->readContainer(tail, sizeof(tail)));
    TEST_ASSERT_EQUAL_UINT32(0, image->readContainer(tail, sizeof(tail)));
}

// A track the image has no chunk for, and a sector that does not exist on the
// track, both have to fail rather than serve the previous sector's bytes.
void test_missing_track_and_sector_fail(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(BLANK, src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/p64");

    TEST_ASSERT_TRUE(image->parseChunks());

    TEST_ASSERT_FALSE(image->seekSector(0, 0, 0));    // below the first track
    TEST_ASSERT_FALSE(image->seekSector(18, 19, 0));  // track 18 holds 19 sectors, 0-18
}


// ---------------------------------------------------------------------------
// A real disk: Wheels 4.4a
// ---------------------------------------------------------------------------

// readHeader() is what a directory listing calls, and it has to parse the chunk
// stream BEFORE delegating to D64MStream::readHeader() - which immediately
// seeks 18/0 and would find no half track table to seek through.
void test_read_header_yields_the_disk_name(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(WHEELS, src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/p64");

    TEST_ASSERT_TRUE(image->readHeader());

    // "WHEELS MASTER" padded to 16 with $A0, exactly as the disk carries it.
    static const uint8_t expected_name[16] = {
        'W', 'H', 'E', 'E', 'L', 'S', ' ', 'M', 'A', 'S', 'T', 'E', 'R', 0xa0, 0xa0, 0xa0
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_name, (const uint8_t*)image->header.name, 16);

    // Disk id "03", then $A0 and the "2A" DOS marker.
    TEST_ASSERT_EQUAL_UINT8('0', (uint8_t)image->header.id_dos[0]);
    TEST_ASSERT_EQUAL_UINT8('3', (uint8_t)image->header.id_dos[1]);
    TEST_ASSERT_EQUAL_UINT8(0xa0, (uint8_t)image->header.id_dos[2]);
    TEST_ASSERT_EQUAL_UINT8('2', (uint8_t)image->header.id_dos[3]);
    TEST_ASSERT_EQUAL_UINT8('A', (uint8_t)image->header.id_dos[4]);
}

// The first directory block, read the way readFile() reads one: a link pair
// followed by eight 32-byte entries. Asserted against the exact contents of the
// real disk - a decode that is subtly wrong produces filenames that are subtly
// wrong, which a "looks printable" check would wave through.
void test_directory_entries_are_exact(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(WHEELS, src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/p64");

    TEST_ASSERT_TRUE(image->parseChunks());
    TEST_ASSERT_TRUE(image->seekSector(18, 1, 0));

    uint8_t block[256];
    TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));

    // This disk's directory is a single block.
    TEST_ASSERT_EQUAL_UINT8(0x00, block[0]);
    TEST_ASSERT_EQUAL_UINT8(0xff, block[1]);

    struct Expected {
        uint8_t file_type;
        uint8_t track;
        uint8_t sector;
        const char* name;
        uint16_t blocks;
    };

    static const Expected expected[8] = {
        { 0x82, 19, 18, "STARTER",      2   },
        { 0x82, 19,  0, "SYSTEM1",      87  },
        { 0x83, 23,  0, "SYSTEM2",      69  },
        { 0x83, 27,  5, "Toolbox 64",   227 },
        { 0x83, 14,  2, "Dashboard 64", 148 },
        { 0x83,  7,  2, "NAMEPLATE",    12  },
        { 0x83,  6, 20, "C1351D",       3   },
        { 0x83,  6,  1, "JOYSTICK",     3   },
    };

    for (int i = 0; i < 8; i++)
    {
        const uint8_t* entry = block + 2 + (i * 32);
        const Expected& want = expected[i];

        char message[64];
        snprintf(message, sizeof(message), "entry %d (%s)", i, want.name);

        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want.file_type, entry[0], message);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want.track, entry[1], message);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want.sector, entry[2], message);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(want.blocks, entry[28] | (entry[29] << 8), message);

        // 16-byte field, the name then $A0 padding.
        size_t length = strlen(want.name);
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((const uint8_t*)want.name, entry + 3, length, message);
        for (size_t p = length; p < 16; p++)
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xa0, entry[3 + p], message);
    }
}

// Following a file's block chain is what actually reading a file does, and it
// is the only thing here that leaves track 18. The two files are in different
// speed zones, so the bit cell timing has to be right for more than one zone,
// and the track cache has to survive being asked for a different track.
//
// Both are GEOS files, so the block count the directory carries is the data
// chain PLUS the single-block GEOS info record that the entry points at
// separately - which is why the accounting below is chain + info == blocks
// rather than chain == blocks.
void test_file_block_chains_walk_to_their_directory_length(void)
{
    struct Chain {
        const char* name;
        uint8_t track;
        uint8_t sector;
        uint8_t info_track;
        uint8_t info_sector;
        uint16_t blocks;
    };

    // STARTER is in speed zone 2, C1351D in zone 3.
    static const Chain chains[2] = {
        { "STARTER", 19, 18, 19,  9, 2 },
        { "C1351D",   6, 20,  6, 11, 3 },
    };

    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(WHEELS, src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/p64");

    TEST_ASSERT_TRUE(image->parseChunks());

    for (size_t i = 0; i < sizeof(chains) / sizeof(chains[0]); i++)
    {
        const Chain& chain = chains[i];

        uint8_t track = chain.track;
        uint8_t sector = chain.sector;
        uint16_t counted = 0;

        while (track != 0 && counted <= chain.blocks)
        {
            TEST_ASSERT_TRUE_MESSAGE(image->seekSector(track, sector, 0), chain.name);

            uint8_t link[2] = { 0, 0 };
            TEST_ASSERT_EQUAL_UINT32(2, image->readContainer(link, 2));
            counted++;

            track = link[0];
            sector = link[1];
        }

        // A chain that ends carries the last used byte offset, not a sector.
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, track, chain.name);

        // The GEOS info record is one block that links nowhere.
        TEST_ASSERT_TRUE_MESSAGE(image->seekSector(chain.info_track, chain.info_sector, 0), chain.name);
        uint8_t info[2] = { 0, 0 };
        TEST_ASSERT_EQUAL_UINT32(2, image->readContainer(info, 2));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, info[0], chain.name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xff, info[1], chain.name);

        TEST_ASSERT_EQUAL_UINT16_MESSAGE(chain.blocks, counted + 1, chain.name);
    }
}


// ---------------------------------------------------------------------------
// Corpus
// ---------------------------------------------------------------------------

// Every sample has to give up its directory track's header sector. A protected
// title's other tracks are a different matter - the reference read logic
// ignores weak pulses by design, and a release can carry a non-standard data
// block id or DOS marker, so those are the format behaving as specified rather
// than a defect. 18/0 parsing as a CBM header is what makes an image usable at
// all, and that is what this pins across the corpus.
void test_corpus_directory_tracks_decode(void)
{
    int checked = 0;

    for (size_t i = 0; i < sizeof(CORPUS) / sizeof(CORPUS[0]); i++)
    {
        std::shared_ptr<FileContainerStream> src;
        auto image = openImage(CORPUS[i], src);
        if (image == nullptr)
            continue;   // corpus not present

        TEST_ASSERT_TRUE_MESSAGE(image->readHeader(), CORPUS[i]);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, image->half_tracks.count(36), CORPUS[i]);

        TEST_ASSERT_TRUE_MESSAGE(image->loadSector(18, 0), CORPUS[i]);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(18, image->sector_buffer[0], CORPUS[i]);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, image->sector_buffer[1], CORPUS[i]);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x41, image->sector_buffer[2], CORPUS[i]);

        checked++;
    }

    if (checked == 0)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/p64");
}


// Every sector of every track, header found and data checksum valid. This is
// the test that pins the rotation-seam overlap, and it is the one that found
// the bug it exists for.
//
// A P64 holds ONE rotation whose position 0 is wherever the imaging hardware
// started, so a sector straddling that point is split across the two ends of
// the bitstream - ends that do not join, since the bit cell phase at 0 has
// nothing to do with the phase where the rotation ran out. Before decodeTrack()
// replayed the pulses for an overlap, exactly the sectors sitting on that seam
// came back wrong: tracks 18, 19, 20 and 23 of this disk each had one bad data
// checksum with its block starting past 95% of the rotation, and tracks 17 and
// 24 each lost a sector whose HEADER was on the seam. Everything else on all 35
// tracks was already fine, which is why nothing smaller than a full sweep - not
// the directory, not a file read - would have caught it.
//
// Wheels is used because it is a full disk: 683 of its blocks are real.
void test_every_sector_of_every_track_decodes(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(WHEELS, src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/p64");

    TEST_ASSERT_TRUE(image->parseChunks());

    for (uint8_t track = 1; track <= 35; track++)
    {
        char message[64];
        snprintf(message, sizeof(message), "track %u", (unsigned)track);
        TEST_ASSERT_TRUE_MESSAGE(image->decodeTrack(track), message);

        uint8_t sectors = (track < 18) ? 21 : (track < 25) ? 19 : (track < 31) ? 18 : 17;
        for (uint8_t sector = 0; sector < sectors; sector++)
        {
            snprintf(message, sizeof(message), "track %u sector %u",
                     (unsigned)track, (unsigned)sector);
            TEST_ASSERT_TRUE_MESSAGE(image->loadSector(track, sector), message);
            TEST_ASSERT_TRUE_MESSAGE(image->last_data_checksum_ok, message);
        }
    }
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    // The chunk stream
    RUN_TEST(test_chunk_walk_finds_half_tracks);
    RUN_TEST(test_chunk_walk_is_idempotent);
    RUN_TEST(test_non_p64_is_refused);

    // Decoding a track
    RUN_TEST(test_track_decodes_to_a_full_rotation);
    RUN_TEST(test_blank_disk_bam_is_exactly_right);
    RUN_TEST(test_every_sector_of_track18_is_readable);
    RUN_TEST(test_every_sector_of_every_track_decodes);
    RUN_TEST(test_seek_sector_positions_read_container);
    RUN_TEST(test_missing_track_and_sector_fail);

    // A real disk
    RUN_TEST(test_read_header_yields_the_disk_name);
    RUN_TEST(test_directory_entries_are_exact);
    RUN_TEST(test_file_block_chains_walk_to_their_directory_length);

    // Corpus
    RUN_TEST(test_corpus_directory_tracks_decode);


    return UNITY_END();
}
