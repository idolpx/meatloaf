// Read-path tests for IDE64 CFS (.hdd) images.
//
// These run against a real image because the CFS layout (boot sector,
// partition directory, sliced NEXTS pointers, balanced data trees) is too
// intertwined to synthesize meaningfully. The image lives in .archive/, which
// is gitignored, so every test skips cleanly when it isn't present.

#include <unity.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/hd/hdd.h"

// The image is opened relative to the project root, which is where
// `pio test -e native` runs the test binary from.
static const char* IMAGE_PATH = ".archive/hdd/ide20201227.hdd";

// The only corpus image with more than one partition, so every selection
// test uses it: slot 0 "C64 OS" (root LBA 5), slot 1 "DISK IMAGES" (32773).
static const char* MULTI_IMAGE_PATH = ".archive/hdd/c64os v1.09-clean.hdd";

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
    using HDDMStream::boot_sector;
    using HDDMStream::seekEntry;
    using HDDMStream::dir_label;
    using HDDMStream::selectPartitionByNumber;
};

static bool imageAvailable()
{
    FILE* fp = fopen(IMAGE_PATH, "rb");
    if (fp == nullptr)
        return false;
    fclose(fp);
    return true;
}

static bool multiImageAvailable()
{
    FILE* fp = fopen(MULTI_IMAGE_PATH, "rb");
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

// Parses a partition table straight out of a file, bypassing the registry's
// MFSOwner-based open (MFSOwner::File aborts under the native stubs).
// Returns false when the image isn't present.
static bool parseImage(const char* path, HDDImageRegistry::Image& img)
{
    auto src = std::make_shared<FileContainerStream>(path);
    if (!src->isOpen())
        return false;
    return HDDImageRegistry::parseInto(src.get(), img);
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

// The CFS 0.11 spec's boot sector table is colspan-encoded:
//   <TH>$0000<TD COLSPAN=3>Unused<TD>DP<TD COLSPAN=4>@Last disk sector
// so "Unused" spans $00-$02, DP is $03, and @Last disk sector is $04-$07.
// The struct had DP at $01 and @Last disk sector at $02-$05.
void test_boot_sector_field_offsets(void)
{
    TEST_ASSERT_EQUAL_UINT32(3, offsetof(HDDMStream::BootSector, default_partition));
    TEST_ASSERT_EQUAL_UINT32(4, offsetof(HDDMStream::BootSector, last_sector));
    TEST_ASSERT_EQUAL_UINT32(8, offsetof(HDDMStream::BootSector, id));
    TEST_ASSERT_EQUAL_UINT32(0x18, offsetof(HDDMStream::BootSector, part_dir));
    TEST_ASSERT_EQUAL_UINT32(0x1C, offsetof(HDDMStream::BootSector, part_dir_backup));
    TEST_ASSERT_EQUAL_UINT32(0x20, offsetof(HDDMStream::BootSector, disk_label));
}

// Corpus invariant that pins @Last disk sector to $04 independently of the
// spec: the partition directory BACKUP sits on the last sector of the disk,
// so @Last disk sector is always @Partition directory backup + 1. With the
// old $02-$05 placement the pointer decodes to garbage and this fails.
void test_last_sector_is_one_past_partition_dir_backup(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());
    TEST_ASSERT_TRUE(image->readHeader());

    uint32_t last   = image->boot_sector.last_sector.getLBA();
    uint32_t backup = image->boot_sector.part_dir_backup.getLBA();

    TEST_ASSERT_TRUE_MESSAGE(image->boot_sector.last_sector.isLBA(),
        "@Last disk sector does not have the LBA bit set - wrong offset");
    TEST_ASSERT_EQUAL_UINT32(backup + 1, last);
}

// DP must come from byte $03. Read it straight out of the file so the test
// does not depend on the struct being right.
void test_default_partition_reads_byte_3(void)
{
    FILE* fp = fopen(IMAGE_PATH, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    uint8_t raw[8] = {0};
    size_t n = fread(raw, 1, sizeof(raw), fp);
    fclose(fp);
    TEST_ASSERT_EQUAL_UINT32(sizeof(raw), n);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());
    TEST_ASSERT_TRUE(image->readHeader());

    TEST_ASSERT_EQUAL_UINT8(raw[3], image->boot_sector.default_partition);
}

// With no partition named in the path, the root of the image is now the
// SELECTED partition's directory rather than the list of partitions. A
// directly constructed stream has no selection, so it falls back to the boot
// sector's DP - which is what the four original read tests rely on.
void test_seekDirectory_empty_path_enters_default_partition(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekDirectory(""));

    // Assert the label BEFORE any seekEntry: the first entry of a CFS root
    // directory is a LABEL entry, and seekEntry() overwrites dir_label from
    // it. (In this image both happen to read "STUFF", so checking afterwards
    // would pass under the old semantics too and prove nothing.)
    TEST_ASSERT_EQUAL_STRING_MESSAGE("STUFF", image->dir_label.c_str(),
        "seekDirectory(\"\") should select the default partition");

    // The real discriminator: entry 1 of the root is now the first FILE
    // inside the default partition. Under the old semantics the root was the
    // partition list, so entry 1 was the partition STUFF itself.
    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)1));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("%DELETED  FILES%", image->entry.filename.c_str(),
        "the image root should list the partition's files, not the partitions");
}

// A path that names a partition still resolves, and reaching a file inside it
// works exactly as before.
void test_seekDirectory_partition_name_still_resolves(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekDirectory("STUFF/UTILS"));
    TEST_ASSERT_EQUAL(HDDMStream::PATH_FILE, image->resolvePath(DEEP_FILE));
}

void test_registry_parses_single_partition_image(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(IMAGE_PATH, img));

    TEST_ASSERT_TRUE(img.valid);
    TEST_ASSERT_EQUAL_STRING("SOCI/SINGULAR", img.disk_label.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, img.parts.size());
    TEST_ASSERT_EQUAL_UINT8(1, img.parts[0].number);        // numbered from 1
    TEST_ASSERT_EQUAL_UINT8(0, img.parts[0].slot);          // from table slot 0
    TEST_ASSERT_EQUAL_UINT8(1, img.parts[0].type);          // CFS
    TEST_ASSERT_EQUAL_STRING("STUFF", img.parts[0].name.c_str());
    TEST_ASSERT_EQUAL_UINT32(5, img.parts[0].root_lba);
    TEST_ASSERT_TRUE(img.parts[0].writeable);
    TEST_ASSERT_FALSE(img.parts[0].hidden);
}

void test_registry_numbers_partitions_from_one(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));

    TEST_ASSERT_EQUAL_STRING("C64 OS", img.disk_label.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, img.parts.size());

    TEST_ASSERT_EQUAL_UINT8(1, img.parts[0].number);
    TEST_ASSERT_EQUAL_UINT8(0, img.parts[0].slot);
    TEST_ASSERT_EQUAL_STRING("C64 OS", img.parts[0].name.c_str());
    TEST_ASSERT_EQUAL_UINT32(5, img.parts[0].root_lba);

    TEST_ASSERT_EQUAL_UINT8(2, img.parts[1].number);
    TEST_ASSERT_EQUAL_UINT8(1, img.parts[1].slot);
    TEST_ASSERT_EQUAL_STRING("DISK IMAGES", img.parts[1].name.c_str());
    TEST_ASSERT_EQUAL_UINT32(32773, img.parts[1].root_lba);

    // No partition is ever numbered 0 - that number means "the currently
    // selected partition" and must never name a table entry.
    for (const auto& p : img.parts)
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, p.number, "partition 0 must not exist");
}

// DP is a SLOT index in the boot sector and must be converted into the
// partition-number space exactly once, at parse time. Slot 0 is DP here, and
// slot 0 is partition 1.
void test_registry_converts_default_partition_from_slot_to_number(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, img.default_part,
        "default_part must hold a partition NUMBER, not the raw DP slot index");
    TEST_ASSERT_EQUAL_UINT8(1, img.selected);
    TEST_ASSERT_NOT_NULL(img.current());
    TEST_ASSERT_EQUAL_STRING("C64 OS", img.current()->name.c_str());
}

void test_registry_lookup_by_number_and_name(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));

    TEST_ASSERT_NOT_NULL(img.byNumber(2));
    TEST_ASSERT_EQUAL_STRING("DISK IMAGES", img.byNumber(2)->name.c_str());
    TEST_ASSERT_NULL(img.byNumber(0));      // never a real partition
    TEST_ASSERT_NULL(img.byNumber(3));
    TEST_ASSERT_NULL(img.byNumber(15));

    TEST_ASSERT_NOT_NULL(img.byName("DISK IMAGES"));
    TEST_ASSERT_EQUAL_UINT8(2, img.byName("DISK IMAGES")->number);
    TEST_ASSERT_NOT_NULL(img.byName("DISK*"));          // wildcards honoured
    TEST_ASSERT_EQUAL_UINT8(2, img.byName("DISK*")->number);
    TEST_ASSERT_NULL(img.byName("NO SUCH PARTITION"));
}

void test_registry_try_select(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));

    TEST_ASSERT_TRUE(img.trySelect(2));
    TEST_ASSERT_EQUAL_UINT8(2, img.selected);

    TEST_ASSERT_TRUE(img.trySelect(1));
    TEST_ASSERT_EQUAL_UINT8(1, img.selected);

    // 0 means "the currently selected partition" and is never selectable, as
    // in DHD. A number past the end leaves the selection alone too.
    TEST_ASSERT_FALSE(img.trySelect(0));
    TEST_ASSERT_EQUAL_UINT8(1, img.selected);
    TEST_ASSERT_FALSE(img.trySelect(7));
    TEST_ASSERT_EQUAL_UINT8(1, img.selected);
}

void test_containerOf_finds_the_hdd_component(void)
{
    TEST_ASSERT_EQUAL_STRING("/sd/x.hdd",
        HDDImageRegistry::containerOf("/sd/x.hdd").c_str());
    TEST_ASSERT_EQUAL_STRING("/sd/x.hdd",
        HDDImageRegistry::containerOf("/sd/x.hdd/STUFF/GAME").c_str());
    TEST_ASSERT_EQUAL_STRING("/sd/X.HDD",
        HDDImageRegistry::containerOf("/sd/X.HDD/STUFF").c_str());
    TEST_ASSERT_EQUAL_STRING("",
        HDDImageRegistry::containerOf("/sd/games/x.d64").c_str());
    // ".hdd" inside a NAME, not as a path component, is not a container.
    TEST_ASSERT_EQUAL_STRING("",
        HDDImageRegistry::containerOf("/sd/my.hddx/file").c_str());
}

// A path may BIND a partition; it must never change the image's selection.
void test_resolve_binds_partition_without_selecting(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));
    TEST_ASSERT_EQUAL_UINT8(1, img.selected);

    std::string rest;
    bool explicit_part = false;
    const HDDPartition* p =
        hddResolvePartitionIn(img, "DISK IMAGES/GAME", &rest, &explicit_part);

    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(explicit_part);
    TEST_ASSERT_EQUAL_UINT8(2, p->number);
    TEST_ASSERT_EQUAL_STRING("GAME", rest.c_str());
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, img.selected,
        "resolving a path must not change the selected partition");
}

void test_resolve_by_number(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));

    // "2" is DISK IMAGES, the SECOND valid entry - not table slot 2, which
    // does not exist in this image.
    std::string rest;
    bool explicit_part = false;
    const HDDPartition* p = hddResolvePartitionIn(img, "2/GAME", &rest, &explicit_part);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(explicit_part);
    TEST_ASSERT_EQUAL_UINT8(2, p->number);
    TEST_ASSERT_EQUAL_STRING("DISK IMAGES", p->name.c_str());
    TEST_ASSERT_EQUAL_STRING("GAME", rest.c_str());
}

// As in DHD (vdrive.c:1324), a partition number of 0 in a path means "the
// currently selected partition". Select 2, then check "0/..." follows it.
void test_resolve_zero_means_the_selected_partition(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));
    TEST_ASSERT_TRUE(img.trySelect(2));

    std::string rest;
    bool explicit_part = false;
    const HDDPartition* p = hddResolvePartitionIn(img, "0/GAME", &rest, &explicit_part);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(explicit_part);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, p->number,
        "0 in a path must follow the selection, as it does in DHD");
    TEST_ASSERT_EQUAL_STRING("GAME", rest.c_str());

    // ...and it is still only a reference: the selection is unchanged.
    TEST_ASSERT_EQUAL_UINT8(2, img.selected);
}

// A component that is not a partition is a FILENAME: resolution falls back to
// the current selection and reports the path unchanged.
void test_resolve_non_partition_falls_back_to_selection(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));
    TEST_ASSERT_TRUE(img.trySelect(2));

    std::string rest;
    bool explicit_part = true;
    const HDDPartition* p = hddResolvePartitionIn(img, "SOMEFILE", &rest, &explicit_part);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_FALSE(explicit_part);
    TEST_ASSERT_EQUAL_UINT8(2, p->number);              // the selection
    TEST_ASSERT_EQUAL_STRING("SOMEFILE", rest.c_str()); // untouched

    // Out-of-range and unparseable numbers are filenames too, never a
    // truncated partition number - this is the 1571-resolved-to-35 bug.
    explicit_part = true;
    TEST_ASSERT_NOT_NULL(hddResolvePartitionIn(img, "1571", &rest, &explicit_part));
    TEST_ASSERT_FALSE(explicit_part);
    TEST_ASSERT_EQUAL_STRING("1571", rest.c_str());
}

// A partition wins over a same-named file; the file stays reachable by number.
void test_resolve_empty_path_uses_selection(void)
{
    HDDImageRegistry::Image img;
    TEST_ASSERT_TRUE(parseImage(MULTI_IMAGE_PATH, img));
    TEST_ASSERT_TRUE(img.trySelect(2));

    std::string rest = "unset";
    bool explicit_part = true;
    const HDDPartition* p = hddResolvePartitionIn(img, "", &rest, &explicit_part);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_FALSE(explicit_part);
    TEST_ASSERT_EQUAL_UINT8(2, p->number);
    TEST_ASSERT_EQUAL_STRING("", rest.c_str());
}

// selected_partition overrides the boot sector's DP.
void test_selected_partition_overrides_default(void)
{
    auto src = std::make_shared<FileContainerStream>(MULTI_IMAGE_PATH);
    TEST_ASSERT_TRUE(src->isOpen());
    auto image = std::make_shared<TestHDDStream>(src);
    image->mode = std::ios_base::in;

    image->selected_partition = 2;          // "DISK IMAGES"
    TEST_ASSERT_TRUE(image->seekDirectory(""));
    TEST_ASSERT_EQUAL_STRING("DISK IMAGES", image->dir_label.c_str());

    image->selected_partition = 1;          // "C64 OS"
    TEST_ASSERT_TRUE(image->seekDirectory(""));
    TEST_ASSERT_EQUAL_STRING("C64 OS", image->dir_label.c_str());

    // 0 is not a partition number - it means "no selection", so the stream
    // falls back to the boot sector's DP (slot 0, which is "C64 OS").
    image->selected_partition = 0;
    TEST_ASSERT_TRUE(image->seekDirectory(""));
    TEST_ASSERT_EQUAL_STRING("C64 OS", image->dir_label.c_str());

    // A number past the end is not a silent fallback - it fails outright,
    // rather than quietly landing on the default partition.
    TEST_ASSERT_FALSE(image->selectPartitionByNumber(9));
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_containerOf_finds_the_hdd_component);

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
    RUN_TEST(test_seekDirectory_empty_path_enters_default_partition);
    RUN_TEST(test_seekDirectory_partition_name_still_resolves);
    RUN_TEST(test_boot_sector_field_offsets);
    RUN_TEST(test_last_sector_is_one_past_partition_dir_backup);
    RUN_TEST(test_default_partition_reads_byte_3);

    RUN_TEST(test_registry_parses_single_partition_image);
    if (multiImageAvailable())
    {
        RUN_TEST(test_registry_numbers_partitions_from_one);
        RUN_TEST(test_registry_converts_default_partition_from_slot_to_number);
        RUN_TEST(test_registry_lookup_by_number_and_name);
        RUN_TEST(test_registry_try_select);

        RUN_TEST(test_resolve_binds_partition_without_selecting);
        RUN_TEST(test_resolve_by_number);
        RUN_TEST(test_resolve_zero_means_the_selected_partition);
        RUN_TEST(test_resolve_non_partition_falls_back_to_selection);
        RUN_TEST(test_resolve_empty_path_uses_selection);
        RUN_TEST(test_selected_partition_overrides_default);
    }

    return UNITY_END();
}
