// Read-path tests for the P81 flux-level 1581 format.
//
// A .p81 is the same container as a .p64 - same header, same chunk stream, same
// range-coded pulses - carrying a completely different physical format. The
// 1581 is an MFM drive, so downstream of the pulses nothing is shared with the
// 1541: the read logic is a fixed 2 us cell instead of the GCR clock/counter,
// the sync is $4489 instead of ten 1 bits, integrity is CRC-16 instead of an
// XOR checksum, sectors are 512 bytes rather than 256, and there are two sides.
//
// These tests therefore pin two separate things: that the shared half still
// works (chunk walk, range decode) and that the 1581-specific half produces
// real CBM structures - a valid D81 header, a directory, and correct block
// chains on both physical heads.
//
// The image is the real one in .data/media/disk/p81 (gitignored, so these skip
// cleanly without it). Nothing here can synthesize a .p81: writing one means
// implementing the range ENCODER and an MFM modulator, neither of which the
// read path has or needs.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/disk/p81.h"

static const char* IMAGE = ".data/media/disk/p81/td1581.p81";

class TestP81Stream : public P81MStream
{
public:
    using P81MStream::P81MStream;
    using P81MStream::cached_physical;
    using P81MStream::half_tracks;
    using P81MStream::last_data_checksum_ok;
    using P81MStream::loadSector;
    using P81MStream::parseChunks;
    using P81MStream::physical_sector;
    using P81MStream::readHeader;
    using P81MStream::sector_buffer;
};

static std::shared_ptr<TestP81Stream> openImage(std::shared_ptr<FileContainerStream>& src)
{
    src = std::make_shared<FileContainerStream>(IMAGE);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestP81Stream>(src);
    // mode is uninitialised on a directly constructed stream - only
    // MFile::getSourceStream() sets it, and garbage carrying the out bit sends
    // seekPath() down the write branch.
    image->mode = std::ios_base::in;
    return image;
}

void setUp(void) {}
void tearDown(void) {}


// ---------------------------------------------------------------------------
// The container, which is shared with .p64
// ---------------------------------------------------------------------------

// A 1581 has no half tracks and two sides, so the chunk table looks nothing
// like a 1541's: every index in the range is a whole cylinder, and each one
// appears twice - once per side, distinguished by bit 7 of the key.
void test_chunk_walk_finds_both_sides(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/disk/p81");

    TEST_ASSERT_TRUE(image->parseChunks());

    int side0 = 0, side1 = 0;
    for (const auto& ht : image->half_tracks)
    {
        if (ht.first & 0x80) side1++; else side0++;
        TEST_ASSERT_TRUE(ht.second.pulses > 0);
        TEST_ASSERT_TRUE(ht.second.size > 0);
        TEST_ASSERT_TRUE(ht.second.offset + ht.second.size <= src->size());
    }

    // 80 cylinders are formatted; the image carries a few more.
    TEST_ASSERT_TRUE(side0 >= 80);
    TEST_ASSERT_TRUE(side1 >= 80);

    // Cylinder 39 (CBM track 40, where the header and BAM live) on both sides.
    TEST_ASSERT_EQUAL_UINT32(1, image->half_tracks.count(39 + P81_FIRST_CYLINDER_HT));
    TEST_ASSERT_EQUAL_UINT32(1, image->half_tracks.count(0x80 | (39 + P81_FIRST_CYLINDER_HT)));
}

// A .p64 signature must be refused by the 1581 stream and vice versa - the two
// share a container but not a format, and reading one as the other produces
// nothing but confusion.
void test_wrong_signature_is_refused(void)
{
    const std::string path = "build_p81_wrong_sig.bin";

    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    uint8_t header[64] = { 0 };
    std::memcpy(header, "P64-1541", 8);
    fwrite(header, 1, sizeof(header), fp);
    fclose(fp);

    auto src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(src->isOpen());

    auto image = std::make_shared<TestP81Stream>(src);
    image->mode = std::ios_base::in;

    TEST_ASSERT_FALSE(image->parseChunks());
    TEST_ASSERT_FALSE(image->readHeader());

    src->close();
    remove(path.c_str());
}


// ---------------------------------------------------------------------------
// MFM
// ---------------------------------------------------------------------------

// The disk header at CBM track 40 sector 0. Getting this back proves the whole
// stack: chunk walk, range decode, flux-to-MFM cells, $4489 sync, the
// clock/data de-interleave, CRC-16, the head inversion and the 512-to-256
// halving. A single wrong step anywhere yields noise.
void test_disk_header_is_a_real_d81_header(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/disk/p81");

    TEST_ASSERT_TRUE(image->parseChunks());
    TEST_ASSERT_TRUE(image->loadSector(40, 0));
    TEST_ASSERT_TRUE(image->last_data_checksum_ok);

    // Link to the first directory block, then the 1581 DOS version byte 'D'.
    TEST_ASSERT_EQUAL_UINT8(40, image->sector_buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(3, image->sector_buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x44, image->sector_buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, image->sector_buffer[3]);

    // The disk name this image actually carries, padded to 16 with $A0.
    static const uint8_t expected_name[16] = {
        '1','5','8','1',' ','U','T','I','L','I','T','Y',' ','V','0','2'
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_name, image->sector_buffer + 4, 16);

    TEST_ASSERT_EQUAL_UINT8(0xa0, image->sector_buffer[0x14]);
    TEST_ASSERT_EQUAL_UINT8(0xa0, image->sector_buffer[0x15]);
    TEST_ASSERT_EQUAL_UINT8('G',  image->sector_buffer[0x16]);
    TEST_ASSERT_EQUAL_UINT8('B',  image->sector_buffer[0x17]);
    TEST_ASSERT_EQUAL_UINT8(0xa0, image->sector_buffer[0x18]);
    // "3D" is the 1581's DOS format marker, as "2A" is the 1541's.
    TEST_ASSERT_EQUAL_UINT8('3',  image->sector_buffer[0x19]);
    TEST_ASSERT_EQUAL_UINT8('D',  image->sector_buffer[0x1a]);
}

// readHeader() goes through the D64 layer, which seeks the header sector and
// reads at the partition's header offset. It has to parse the chunk stream
// first - the same ordering trap p64 has.
void test_read_header_yields_the_disk_name(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/disk/p81");

    TEST_ASSERT_TRUE(image->readHeader());

    static const uint8_t expected_name[16] = {
        '1','5','8','1',' ','U','T','I','L','I','T','Y',' ','V','0','2'
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_name, (const uint8_t*)image->header.name, 16);
    TEST_ASSERT_EQUAL_UINT8('G', (uint8_t)image->header.id_dos[0]);
    TEST_ASSERT_EQUAL_UINT8('B', (uint8_t)image->header.id_dos[1]);
    TEST_ASSERT_EQUAL_UINT8('3', (uint8_t)image->header.id_dos[3]);
    TEST_ASSERT_EQUAL_UINT8('D', (uint8_t)image->header.id_dos[4]);
}

// Two logical blocks share one physical sector, and the halving has to put them
// the right way round: block 2n is the low half, 2n+1 the high half. Reading
// them back to back out of one cached physical sector is also the path that
// would silently serve the same 256 bytes twice if the halving were dropped.
void test_two_blocks_share_one_physical_sector(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/disk/p81");

    TEST_ASSERT_TRUE(image->parseChunks());

    TEST_ASSERT_TRUE(image->loadSector(40, 0));
    uint8_t low[256];
    std::memcpy(low, image->sector_buffer, sizeof(low));

    TEST_ASSERT_TRUE(image->loadSector(40, 1));
    uint8_t high[256];
    std::memcpy(high, image->sector_buffer, sizeof(high));

    // Both halves came out of the same 512-byte sector, in order.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(image->physical_sector, low, 256);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(image->physical_sector + 256, high, 256);
    TEST_ASSERT_TRUE(std::memcmp(low, high, 256) != 0);
}

// Every block of the directory track, on both heads. Sectors 0-19 are head 0
// and 20-39 are head 1, so this is the test that fails if the side inversion is
// wrong: the head-1 half would decode cleanly but come from the wrong surface.
void test_every_block_of_the_directory_track_reads(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/disk/p81");

    TEST_ASSERT_TRUE(image->parseChunks());

    for (uint8_t sector = 0; sector < 40; sector++)
    {
        char message[48];
        snprintf(message, sizeof(message), "track 40 sector %u", (unsigned)sector);
        TEST_ASSERT_TRUE_MESSAGE(image->loadSector(40, sector), message);
        TEST_ASSERT_TRUE_MESSAGE(image->last_data_checksum_ok, message);
    }
}

// A sweep of both heads across the whole disk: 80 cylinders x 40 blocks. This
// is the P81 counterpart of the p64 full-disk sweep, and it is what would catch
// a rotation-seam problem in the MFM path - a sector straddling the seam is
// exactly as broken here as it was there, and the overlap replay is inherited
// rather than reimplemented, so it needs its own proof.
void test_every_block_of_every_track_reads(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/disk/p81");

    TEST_ASSERT_TRUE(image->parseChunks());

    for (uint8_t track = 1; track <= 80; track++)
    {
        for (uint8_t sector = 0; sector < 40; sector++)
        {
            char message[48];
            snprintf(message, sizeof(message), "track %u sector %u",
                     (unsigned)track, (unsigned)sector);
            TEST_ASSERT_TRUE_MESSAGE(image->loadSector(track, sector), message);
            TEST_ASSERT_TRUE_MESSAGE(image->last_data_checksum_ok, message);
        }
    }
}

// seekSector() plus readContainer() is the path the D64 layer actually uses,
// and the offset has to land inside the block rather than the file.
void test_seek_sector_positions_read_container(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/disk/p81");

    TEST_ASSERT_TRUE(image->parseChunks());

    TEST_ASSERT_TRUE(image->seekSector(40, 0, 0));
    uint8_t link[2] = { 0, 0 };
    TEST_ASSERT_EQUAL_UINT32(2, image->readContainer(link, 2));
    TEST_ASSERT_EQUAL_UINT8(40, link[0]);
    TEST_ASSERT_EQUAL_UINT8(3, link[1]);

    // The DOS marker only lands on "3D" if the offset applied to the block.
    TEST_ASSERT_TRUE(image->seekSector(40, 0, 0x19));
    uint8_t marker[2] = { 0, 0 };
    TEST_ASSERT_EQUAL_UINT32(2, image->readContainer(marker, 2));
    TEST_ASSERT_EQUAL_UINT8('3', marker[0]);
    TEST_ASSERT_EQUAL_UINT8('D', marker[1]);

    // A read can never run past the end of the block it is positioned in.
    TEST_ASSERT_TRUE(image->seekSector(40, 0, 250));
    uint8_t tail[64];
    TEST_ASSERT_EQUAL_UINT32(6, image->readContainer(tail, sizeof(tail)));
    TEST_ASSERT_EQUAL_UINT32(0, image->readContainer(tail, sizeof(tail)));

    TEST_ASSERT_FALSE(image->seekSector(0, 0, 0));    // below the first track
    TEST_ASSERT_FALSE(image->seekSector(81, 0, 0));   // past the last
    TEST_ASSERT_FALSE(image->seekSector(40, 40, 0));  // 40 blocks, 0-39
}

// The directory, walked the way a listing walks it: from the header's link,
// following block links and reading 8 entries per block. A 1581 directory lives
// entirely on track 40, so this also exercises repeated reads within one
// decoded cylinder.
void test_directory_walk_finds_entries(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/disk/p81");

    TEST_ASSERT_TRUE(image->parseChunks());

    uint8_t track = 40, sector = 3;
    int entries = 0;
    int blocks = 0;

    while (track != 0 && blocks < 40)
    {
        TEST_ASSERT_TRUE(image->seekSector(track, sector, 0));

        uint8_t block[256];
        TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));
        blocks++;

        for (int i = 0; i < 8; i++)
        {
            const uint8_t* entry = block + 2 + (i * 32);
            if ((entry[0] & 0x0f) == 0)
                continue;   // scratched or unused

            // A real entry points at a block that exists on this disk.
            TEST_ASSERT_TRUE(entry[1] >= 1 && entry[1] <= 80);
            TEST_ASSERT_TRUE(entry[2] < 40);
            entries++;
        }

        track = block[0];
        sector = block[1];
    }

    TEST_ASSERT_TRUE(blocks > 0);
    TEST_ASSERT_TRUE(entries > 0);
}

// Following real files' block chains, which is the ONLY thing here that
// constrains the sector ORDER within a track.
//
// Everything else - the header, the sweep, the CRCs - would still pass if the
// logical-to-physical sector mapping shuffled blocks within a head: all 3200
// blocks would decode cleanly, just in the wrong order. A chain walk cannot be
// fooled that way. Each link names the next block, so a mapping that is off
// anywhere lands on a block holding someone else's data and the walk runs long,
// runs short, or dies - and the directory's own block count is the answer to
// check it against.
//
// This also covers the head boundary: a chain running past block 19 crosses
// from head 0 to head 1, which is a different chunk entirely.
void test_file_chains_match_their_directory_block_counts(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/disk/p81");

    TEST_ASSERT_TRUE(image->parseChunks());

    struct Entry { uint8_t track, sector; uint16_t blocks; char name[17]; };
    std::vector<Entry> files;

    uint8_t track = 40, sector = 3;
    for (int guard = 0; guard < 40 && track != 0; guard++)
    {
        TEST_ASSERT_TRUE(image->seekSector(track, sector, 0));
        uint8_t block[256];
        TEST_ASSERT_EQUAL_UINT32(sizeof(block), image->readContainer(block, sizeof(block)));

        for (int i = 0; i < 8; i++)
        {
            const uint8_t* e = block + 2 + (i * 32);
            // Closed SEQ/PRG/USR only. A REL file's count includes its side
            // sectors, an open file's count cannot be trusted, and a 1581
            // directory can also hold CBM sub-partition entries (type $85 -
            // this image has one, PIC.DIR) whose "blocks" is the SIZE of a
            // partition area rather than the length of a chain. Walking any of
            // those as a chain proves nothing.
            uint8_t type = e[0];
            if ((type & 0x80) == 0) continue;
            uint8_t kind = (uint8_t)(type & 0x0f);
            if (kind < 1 || kind > 3) continue;

            Entry entry;
            entry.track = e[1];
            entry.sector = e[2];
            entry.blocks = (uint16_t)(e[28] | (e[29] << 8));
            std::memcpy(entry.name, e + 3, 16);
            entry.name[16] = 0;
            for (int c = 15; c >= 0 && (uint8_t)entry.name[c] == 0xa0; c--)
                entry.name[c] = 0;

            if (entry.blocks > 0 && entry.track >= 1 && entry.track <= 80 && entry.sector < 40)
                files.push_back(entry);
        }

        track = block[0];
        sector = block[1];
    }

    TEST_ASSERT_TRUE_MESSAGE(files.size() >= 3, "not enough files to prove the mapping");

    int crossed_heads = 0;

    for (const Entry& f : files)
    {
        uint8_t t = f.track, s = f.sector;
        uint16_t counted = 0;
        bool saw_head0 = false, saw_head1 = false;

        while (t != 0 && counted <= f.blocks)
        {
            char message[80];
            snprintf(message, sizeof(message), "%s at %u/%u", f.name, (unsigned)t, (unsigned)s);
            TEST_ASSERT_TRUE_MESSAGE(image->seekSector(t, s, 0), message);

            if (s < 20) saw_head0 = true; else saw_head1 = true;

            uint8_t link[2] = { 0, 0 };
            TEST_ASSERT_EQUAL_UINT32(2, image->readContainer(link, 2));
            counted++;

            t = link[0];
            s = link[1];
        }

        char message[80];
        snprintf(message, sizeof(message), "%s chain ended cleanly", f.name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, t, message);

        snprintf(message, sizeof(message), "%s block count", f.name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(f.blocks, counted, message);

        if (saw_head0 && saw_head1)
            crossed_heads++;
    }

    // At least one chain has to have crossed from head 0 to head 1, or the head
    // split is untested by this.
    TEST_ASSERT_TRUE_MESSAGE(crossed_heads > 0, "no chain crossed the head boundary");
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_chunk_walk_finds_both_sides);
    RUN_TEST(test_wrong_signature_is_refused);

    RUN_TEST(test_disk_header_is_a_real_d81_header);
    RUN_TEST(test_read_header_yields_the_disk_name);
    RUN_TEST(test_two_blocks_share_one_physical_sector);
    RUN_TEST(test_every_block_of_the_directory_track_reads);
    RUN_TEST(test_seek_sector_positions_read_container);
    RUN_TEST(test_directory_walk_finds_entries);
    RUN_TEST(test_file_chains_match_their_directory_block_counts);
    RUN_TEST(test_every_block_of_every_track_reads);

    return UNITY_END();
}
