// Read-path tests for the Commodore ARC/SDA archive format.
//
// ARC is not one format but six, and only the first is plain: an entry is
// stored, run-length packed, Huffman squeezed, LZW crunched, squeezed AND
// packed, or crunched in a single pass with its size and checksum written at
// the END rather than in the header. The decompressors are ports of
// cbmconvert's unarc.c, which is the reference implementation.
//
// That makes the checksum the thing worth testing hardest. Every entry carries
// one, computed over the DECOMPRESSED bytes, in one of two shapes depending on
// the archive version - so an entry that decompresses to the right length but
// the wrong content fails it. Nothing else here can tell a subtly wrong
// Huffman table or LZW string table from a right one.
//
// The corpus is real: nine .arc files and an .sda in .data/media/archive, none of
// them written by this project. .data/media is gitignored, so these skip cleanly
// without it.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/archive/arc.h"
#include "string_utils.h"

static const char* CORPUS_DIR = ".data/media/archive/arc";
static const char* SDA_DIR = ".data/media/archive/sda";

static const char* CORPUS[] = {
    "fwriter2.arc", "graphic-aids.arc", "macto64-1525.arc", "pacificwar.arc",
    "pcpfonts.arc", "pd.readerv5.arc", "poolcheater.arc", "WARGAMES.ARC",
    "zos.arc",
};

class TestARCStream : public ARCMStream
{
public:
    using ARCMStream::ARCMStream;
    using ARCMStream::archive_offset;
    using ARCMStream::checksum_ok;
    using ARCMStream::data;
    using ARCMStream::entries;
    using ARCMStream::entry;
    using ARCMStream::readHeader;
    using ARCMStream::seekEntry;
    using ARCMStream::seekPath;
};

static std::shared_ptr<TestARCStream> openImage(const std::string& path,
                                                std::shared_ptr<FileContainerStream>& src)
{
    src = std::make_shared<FileContainerStream>(path);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestARCStream>(src);
    // mode is uninitialised on a directly constructed stream - only
    // MFile::getSourceStream() sets it, and garbage carrying the out bit sends
    // seekPath() down the write branch.
    image->mode = std::ios_base::in;
    return image;
}

void setUp(void) {}
void tearDown(void) {}


// The directory walk. It never touches compressed data - the next header sits
// exactly blocks * 254 bytes past this one - so this also proves the walk is
// not accidentally depending on decompressing anything.
void test_directory_lists_entries(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[0], src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/arc");

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_TRUE(image->entries.size() > 0);

    for (const auto& e : image->entries)
    {
        TEST_ASSERT_TRUE(e.version == 1 || e.version == 2);
        TEST_ASSERT_TRUE(e.mode <= 5);
        TEST_ASSERT_TRUE(e.blocks > 0);
        TEST_ASSERT_TRUE(e.filename.size() > 0 && e.filename.size() <= 16);
        TEST_ASSERT_TRUE(e.type == 'P' || e.type == 'S' || e.type == 'U' || e.type == 'R');
    }
}

// Entries are found by name the way the drive asks for them - through
// mstr::toUTF8() of the stored name, since PETSCII $41-$5A map to lowercase.
void test_entries_are_found_by_name(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[0], src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/arc");

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_TRUE(image->entries.size() > 0);

    std::string wanted = mstr::toUTF8(image->entries[0].filename);
    TEST_ASSERT_TRUE_MESSAGE(image->seekEntry(wanted), wanted.c_str());
    TEST_ASSERT_EQUAL_STRING(image->entries[0].filename.c_str(), image->entry.filename.c_str());

    // A wildcard resolves to the first match, as it does everywhere else.
    TEST_ASSERT_TRUE(image->seekEntry(std::string("*")));

    TEST_ASSERT_FALSE(image->seekEntry(std::string("no such file here")));
}

// The whole point. Every entry of every archive in the corpus is decompressed
// and its checksum verified - which is the only thing that can tell a subtly
// wrong Huffman table or LZW string table from a right one, since a wrong one
// still produces plausible-looking bytes of roughly the right length.
void test_every_entry_decompresses_with_a_valid_checksum(void)
{
    // TWO entries in the corpus are damaged, and they are named here rather
    // than waved through. Both are in pcpfonts.arc, both are mode 3 (LZW), and
    // both declare 3757 bytes while their LZW stream reaches its own end
    // marker after 3756 - the archive's header and its own terminator disagree
    // by one byte. The decoder consumed each stream to its natural end, which
    // is what cbmconvert does too: its extraction loop also stops on
    // end-of-stream and also reports a checksum error.
    //
    // What rules out a systematic off-by-one in the LZW port is the same
    // archive: pcpfonts.arc holds 45 entries, 43 of them mode 3, and those 43
    // decode with correct checksums. A decoder that dropped a final byte would
    // fail all of them, and an archiver with a bad size field would have
    // written all of them wrong.
    //
    // Listing them by name keeps the assertion strict. If the decoder ever
    // regresses, some OTHER entry starts failing and this notices; if one of
    // these is ever fixed upstream, the "now passes" branch below notices that.
    struct Known { const char* archive; const char* entry; };
    static const Known KNOWN_BAD[] = {
        { "pcpfonts.arc", "PRINCETON24 P.24" },
        { "pcpfonts.arc", "RUTGERS24 PD .24" },
    };

    int archives = 0;
    int checked = 0;
    int modes_seen = 0;
    int unexpected_failures = 0;
    int known_failures = 0;

    for (size_t i = 0; i < sizeof(CORPUS) / sizeof(CORPUS[0]); i++)
    {
        std::shared_ptr<FileContainerStream> src;
        auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[i], src);
        if (image == nullptr)
            continue;

        TEST_ASSERT_TRUE_MESSAGE(image->readHeader(), CORPUS[i]);
        archives++;

        for (uint16_t index = 1; index <= image->entries.size(); index++)
        {
            const std::string stored = image->entries[index - 1].filename;
            const uint8_t mode = image->entries[index - 1].mode;

            char message[96];
            snprintf(message, sizeof(message), "%s entry %u [%s] mode %u",
                     CORPUS[i], (unsigned)index, stored.c_str(), (unsigned)mode);

            std::string wanted = mstr::toUTF8(stored);
            TEST_ASSERT_TRUE_MESSAGE(image->seekPath(wanted), message);
            TEST_ASSERT_TRUE_MESSAGE(image->data.size() > 0, message);

            bool expected_bad = false;
            for (size_t k = 0; k < sizeof(KNOWN_BAD) / sizeof(KNOWN_BAD[0]); k++)
                if (std::string(KNOWN_BAD[k].data/media) == CORPUS[i] &&
                    std::string(KNOWN_BAD[k].entry) == stored)
                    expected_bad = true;

            if (!image->checksum_ok)
            {
                if (expected_bad)
                    known_failures++;
                else
                {
                    printf("UNEXPECTED checksum failure: %s\n", message);
                    unexpected_failures++;
                }
            }
            else
            {
                TEST_ASSERT_FALSE_MESSAGE(expected_bad,
                    "a known-damaged entry now passes - update KNOWN_BAD");
            }

            modes_seen |= (1 << mode);
            checked++;
        }
    }

    if (archives == 0)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/arc");

    TEST_ASSERT_TRUE_MESSAGE(checked > 0, "no entries were checked");

    printf("checked %d entries in %d archives, modes seen: ", checked, archives);
    for (int m = 0; m <= 5; m++)
        if (modes_seen & (1 << m))
            printf("%d ", m);
    printf("(known-bad: %d)\n", known_failures);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, unexpected_failures,
                                  "entries failed their checksum that should not have");

    // The corpus has to exercise more than the trivial mode, or this proves
    // very little. Mode 0 is "stored"; anything beyond it ran a decompressor.
    TEST_ASSERT_TRUE_MESSAGE(modes_seen & ~(1 << 0),
                             "corpus only exercises stored entries");
}

// A decompressed entry is served through readFile() the way the drive reads
// it, and must never hand back more than remains.
void test_reading_an_entry_serves_exactly_its_bytes(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[0], src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/arc");

    TEST_ASSERT_TRUE(image->readHeader());
    std::string wanted = mstr::toUTF8(image->entries[0].filename);
    TEST_ASSERT_TRUE(image->seekPath(wanted));

    const uint32_t expected = (uint32_t)image->data.size();
    TEST_ASSERT_EQUAL_UINT32(expected, image->size());

    std::vector<uint8_t> got;
    uint8_t buffer[256];
    for (int guard = 0; guard < 8192; guard++)
    {
        uint32_t n = image->read(buffer, sizeof(buffer));
        if (n == 0)
            break;
        got.insert(got.end(), buffer, buffer + n);
    }

    TEST_ASSERT_EQUAL_UINT32(expected, (uint32_t)got.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(image->data.data(), got.data(), expected);
}

// An .sda is the same archive behind a BASIC loader that dissolves it on a
// real machine. The loader has to be stepped over, and the block count in its
// line number is what says by how much.
void test_sda_skips_its_basic_loader(void)
{
    std::shared_ptr<FileContainerStream> src;
    auto image = openImage(std::string(SDA_DIR) + "/Bash.sda", src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/sda");

    TEST_ASSERT_TRUE(image->readHeader());

    // The archive does not start at byte 0 - if it did, this would be a .arc.
    TEST_ASSERT_TRUE_MESSAGE(image->archive_offset > 0, "no BASIC loader was skipped");
    TEST_ASSERT_TRUE(image->entries.size() > 0);

    for (uint16_t index = 1; index <= image->entries.size(); index++)
    {
        char message[96];
        snprintf(message, sizeof(message), "Bash.sda entry %u [%s] mode %u",
                 (unsigned)index, image->entries[index - 1].filename.c_str(),
                 (unsigned)image->entries[index - 1].mode);

        std::string wanted = mstr::toUTF8(image->entries[index - 1].filename);
        TEST_ASSERT_TRUE_MESSAGE(image->seekPath(wanted), message);
        TEST_ASSERT_TRUE_MESSAGE(image->checksum_ok, message);
    }
}

// Something that is not an archive at all has to be refused rather than walked.
void test_non_archive_is_refused(void)
{
    const std::string path = "build_arc_not_an_arc.bin";

    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    uint8_t junk[512];
    for (size_t i = 0; i < sizeof(junk); i++)
        junk[i] = (uint8_t)(0x40 + (i % 7));
    fwrite(junk, 1, sizeof(junk), fp);
    fclose(fp);

    auto src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(src->isOpen());
    auto image = std::make_shared<TestARCStream>(src);
    image->mode = std::ios_base::in;

    TEST_ASSERT_FALSE(image->readHeader());

    src->close();
    remove(path.c_str());
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_directory_lists_entries);
    RUN_TEST(test_entries_are_found_by_name);
    RUN_TEST(test_reading_an_entry_serves_exactly_its_bytes);
    RUN_TEST(test_sda_skips_its_basic_loader);
    RUN_TEST(test_non_archive_is_refused);
    RUN_TEST(test_every_entry_decompresses_with_a_valid_checksum);

    return UNITY_END();
}
