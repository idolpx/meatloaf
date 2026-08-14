// Entry-lookup tests for the M2I (MMC2IEC/sd2iec text index) format.
//
// The index is the whole filesystem for M2I: every entry the format knows
// about comes from readHeader() parsing the text file. These tests drive a
// stream the way a LOAD does - straight to entry lookup, with no directory
// listing first.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "../test_disk_write/file_container_stream.h"
#include "media/disk/m2i.h"

// Written fresh by setUp() and removed by tearDown(), in the working
// directory, matching the disk-write suite's "build_*" artifact convention.
static const char* M2I_PATH = "build_test_m2i.m2i";

// A minimal but realistic index: title line, then "T:DOSNAME.EXT :CBMNAME"
// records. The '-' record is a free slot and must be dropped; the 'D' record
// is a DEL separator line and is listed but never loadable.
static const char* M2I_TEXT =
    "TEST DISK       \r\n"
    "P:GAME    .PRG :GAME\r\n"
    "P:LOADER  .PRG :LOADER\r\n"
    "D:         :----------------\r\n"
    "-:         :\r\n"
    "S:NOTES   .SEQ :NOTES\r\n";

// seekEntry() is protected; the tests drive it the way seekPath() does.
class TestM2IStream : public M2IMStream
{
public:
    using M2IMStream::M2IMStream;
    using M2IMStream::entries;
    using M2IMStream::entry;
    using M2IMStream::readHeader;
    using M2IMStream::seekEntry;
    using M2IMStream::title;
};

static std::shared_ptr<TestM2IStream> openIndex()
{
    auto src = std::make_shared<FileContainerStream>(M2I_PATH);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestM2IStream>(src);
    // A directly constructed media stream has an uninitialised `mode` - only
    // MFile::getSourceStream() sets it.
    image->mode = std::ios_base::in;
    return image;
}

void setUp(void)
{
    FILE* fp = fopen(M2I_PATH, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite(M2I_TEXT, 1, strlen(M2I_TEXT), fp);
    fclose(fp);
}

void tearDown(void)
{
    remove(M2I_PATH);
}

// The bug: a stream that has not been through a directory listing has never
// parsed the index, so `entries` is empty and every name lookup fails.
// M2IMFile::rewindDirectory() calls readHeader() explicitly, which is why
// listing worked and loading did not.
void test_seekEntry_by_name_parses_index_lazily(void)
{
    auto image = openIndex();
    TEST_ASSERT_NOT_NULL(image.get());

    // seekEntry() compares the query against mstr::toUTF8(entry.cbmname), so
    // the query has to be in that same domain - which is what the drive hands
    // it. Converting the literal here puts both sides through one function.
    TEST_ASSERT_TRUE_MESSAGE(image->seekEntry(mstr::toUTF8("LOADER")),
        "seekEntry() failed on a stream whose index had not been parsed");
    TEST_ASSERT_EQUAL_STRING("LOADER", image->entry.cbmname.c_str());
    TEST_ASSERT_EQUAL_STRING("LOADER  .PRG", image->entry.dosname.c_str());
}

// LOAD"*" - the first loadable entry. Same lookup path, no name conversion.
void test_seekEntry_wildcard_parses_index_lazily(void)
{
    auto image = openIndex();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekEntry(std::string("*")));
    TEST_ASSERT_EQUAL_STRING("GAME", image->entry.cbmname.c_str());
}

// The same lookup by index, which is what a directory listing walks.
void test_seekEntry_by_index_parses_index_lazily(void)
{
    auto image = openIndex();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)1));
    TEST_ASSERT_EQUAL_STRING("GAME", image->entry.cbmname.c_str());
}

// Isolates the cause: the identical lookup succeeds once the index has been
// parsed, so nothing about the name itself is wrong.
void test_seekEntry_succeeds_after_explicit_readHeader(void)
{
    auto image = openIndex();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_TRUE(image->seekEntry(mstr::toUTF8("LOADER")));
    TEST_ASSERT_EQUAL_STRING("LOADER", image->entry.cbmname.c_str());
}

// The lazy parse must produce the same index the explicit one does: free
// slots dropped, DEL separator kept, title trimmed.
void test_lazy_parse_matches_explicit_parse(void)
{
    auto image = openIndex();
    TEST_ASSERT_NOT_NULL(image.get());

    // Triggers the lazy read.
    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)1));

    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)image->entries.size());
    TEST_ASSERT_EQUAL_STRING("TEST DISK", image->title.c_str());
    TEST_ASSERT_EQUAL('D', image->entries[2].type);
    TEST_ASSERT_EQUAL_STRING("NOTES", image->entries[3].cbmname.c_str());
}

// A name that isn't in the index must still report not-found rather than
// looping or reporting the wrong entry.
void test_missing_name_is_not_found(void)
{
    auto image = openIndex();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_FALSE(image->seekEntry(mstr::toUTF8("NOPE")));
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_seekEntry_by_name_parses_index_lazily);
    RUN_TEST(test_seekEntry_wildcard_parses_index_lazily);
    RUN_TEST(test_seekEntry_by_index_parses_index_lazily);
    RUN_TEST(test_seekEntry_succeeds_after_explicit_readHeader);
    RUN_TEST(test_lazy_parse_matches_explicit_parse);
    RUN_TEST(test_missing_name_is_not_found);
    return UNITY_END();
}
