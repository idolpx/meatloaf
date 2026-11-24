// REL files kept as P00 (.r00) containers on a plain filesystem: a round trip
// through P00MFile::createRelStream / P00MStream::relStorage on a temp file.
#include <unity.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "media/file/p00.h"
#include "media/file/rel.h"
#include "../test_disk_write/file_container_stream.h"

static const char* kPath = "build_rel_p00.r00";

// The minimum MFile needs to exist natively: meatloaf.cpp is not compiled here.
MFile::MFile(std::string p) { path = p; url = p; name = "scores.r00"; }

// A plain file on the host standing in for the flash/SD source file.
class HostFile : public MFile {
public:
    HostFile(std::string p) : MFile(p) {}
    bool exists() override { struct stat st; return stat(kPath, &st) == 0; }
    bool remove() override { return ::remove(kPath) == 0; }
    bool isDirectory() override { return false; }
    bool rewindDirectory() override { return false; }
    MFile* getNextFileInDir() override { return nullptr; }
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override { return is; }
    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode) override
    {
        if (!(mode & std::ios_base::in)) {            // out only: create/truncate
            FILE* f = fopen(kPath, "wb");
            if (f) fclose(f);
        }
        auto s = std::make_shared<FileContainerStream>(kPath);
        s->mode = mode;
        return s->isOpen() ? s : nullptr;
    }
    uint32_t refusedWrites = 0;
};

static std::unique_ptr<P00MFile> make_file()
{
    auto f = std::make_unique<P00MFile>(kPath);
    f->name = "scores.r00";
    f->sourceFile = new HostFile(kPath);
    return f;
}

void setUp(void) { remove(kPath); }
void tearDown(void) { remove(kPath); }

static std::vector<uint8_t> rec(uint8_t seed, uint32_t len)
{
    std::vector<uint8_t> v(len);
    for (uint32_t i = 0; i < len; i++) v[i] = (uint8_t)(seed + i * 3 + 1);
    return v;
}

void test_host_filename_basic(void)
{
    TEST_ASSERT_EQUAL_STRING("SCORES.r00", relHostFilename("SCORES").c_str());
}

void test_host_filename_replaces_bad_characters(void)
{
    const char* bad = "/\\:*?\"<>|";
    for (const char* p = bad; *p; p++) {
        std::string in = std::string("A") + *p + "B";
        TEST_ASSERT_EQUAL_STRING("A_B.r00", relHostFilename(in).c_str());
    }
    for (int c = 0; c < 0x20; c++) {
        std::string in = std::string("A") + (char)c + "B";
        TEST_ASSERT_EQUAL_STRING("A_B.r00", relHostFilename(in).c_str());
    }
    TEST_ASSERT_EQUAL_STRING("A_B.r00", relHostFilename("A\xEE\x80\x80" "B").c_str());   // U+E000
    TEST_ASSERT_EQUAL_STRING("A_B.r00", relHostFilename("A\xEF\xBF\xBD" "B").c_str());   // U+FFFD
    TEST_ASSERT_EQUAL_STRING("\xC3\xA9.r00", relHostFilename("\xC3\xA9").c_str());       // e-acute kept
    TEST_ASSERT_EQUAL_STRING(relHostFilename("a:b").c_str(), relHostFilename("a:b").c_str());
}

void test_host_filename_empty(void)
{
    TEST_ASSERT_EQUAL_STRING("_.r00", relHostFilename("").c_str());
}

void test_p00_rel_round_trip(void)
{
    const uint32_t LEN = 30;
    {
        auto f = make_file();
        auto s = f->createRelStream(LEN);
        TEST_ASSERT_NOT_NULL(s.get());
        auto* rel = static_cast<RELMStream*>(s.get());
        TEST_ASSERT_EQUAL_UINT8(0, rel->position(1, 1));   // empty file: record 1 ok
        for (int r = 0; r < 4; r++) {
            uint8_t ps = rel->position(r + 1, 1);   // 50 past the end: the write grows
            TEST_ASSERT_TRUE(ps == 0 || ps == 50);
            auto d = rec((uint8_t)r, 20);
            TEST_ASSERT_EQUAL_UINT32(20, rel->write(d.data(), 20));
            rel->endRecord();
        }
        rel->close();
    }
    // Reopen through the normal container path.
    HostFile hf(kPath);
    auto src = hf.getSourceStream(std::ios_base::in | std::ios_base::out);
    TEST_ASSERT_NOT_NULL(src.get());
    P00MFile pf(kPath);
    auto st = std::static_pointer_cast<P00MStream>(pf.getDecodedStream(src));
    st->mode = std::ios_base::in | std::ios_base::out;
    TEST_ASSERT_EQUAL_UINT8(LEN, st->relRecordLength());
    auto storage = st->relStorage();
    TEST_ASSERT_NOT_NULL(storage.get());
    RELMStream rel(std::move(storage), LEN);
    TEST_ASSERT_EQUAL_UINT32(4, rel.recordCount());
    for (int r = 0; r < 4; r++) {
        TEST_ASSERT_EQUAL_UINT8(0, rel.position(r + 1, 1));
        uint8_t got[30];
        uint32_t n = rel.read(got, sizeof(got));
        auto want = rec((uint8_t)r, 20);
        TEST_ASSERT_EQUAL_UINT32(20, n);
        TEST_ASSERT_EQUAL_MEMORY(want.data(), got, 20);
    }
}

void test_header_holds_cbm_name_and_flag(void)
{
    auto f = make_file();
    TEST_ASSERT_NOT_NULL(f->createRelStream(30).get());
    FILE* fp = fopen(kPath, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    uint8_t h[26];
    TEST_ASSERT_EQUAL_UINT32(26, (uint32_t)fread(h, 1, 26, fp));
    fclose(fp);
    TEST_ASSERT_EQUAL_MEMORY("C64File\0", h, 8);
    TEST_ASSERT_EQUAL_MEMORY("\x53\x43\x4F\x52\x45\x53" "\0\0\0\0\0\0\0\0\0\0", h + 8, 16);  // SCORES in PETSCII
    TEST_ASSERT_EQUAL_UINT8(0, h[24]);
    TEST_ASSERT_EQUAL_UINT8(30, h[25]);
}

void test_existing_file_is_not_clobbered(void)
{
    FILE* fp = fopen(kPath, "wb");
    fputs("keep me", fp);
    fclose(fp);
    auto f = make_file();
    TEST_ASSERT_NULL(f->createRelStream(30).get());
    fp = fopen(kPath, "rb");
    char b[8] = {0};
    TEST_ASSERT_EQUAL_UINT32(7, (uint32_t)fread(b, 1, 7, fp));
    fclose(fp);
    TEST_ASSERT_EQUAL_STRING("keep me", b);
}

void test_non_writable_container_gives_write_protect(void)
{
    {
        auto f = make_file();
        auto s = f->createRelStream(30);
        TEST_ASSERT_NOT_NULL(s.get());
        auto* rel = static_cast<RELMStream*>(s.get());
        rel->position(1, 1);
        uint8_t d[5] = {1, 2, 3, 4, 5};
        rel->write(d, 5);
        rel->endRecord();
        rel->close();
    }
    HostFile hf(kPath);
    auto src = hf.getSourceStream(std::ios_base::in);
    P00MFile pf(kPath);
    auto st = std::static_pointer_cast<P00MStream>(pf.getDecodedStream(src));
    st->mode = std::ios_base::in;
    auto storage = st->relStorage();
    TEST_ASSERT_NOT_NULL(storage.get());
    TEST_ASSERT_FALSE(storage->writable());
    TEST_ASSERT_EQUAL_UINT8(26, storage->grow(storage->size() + 30));
    TEST_ASSERT_EQUAL_UINT32(0, storage->writeAt(0, (const uint8_t*)"x", 1));
}

void test_plain_p00_is_not_rel(void)
{
    FILE* fp = fopen(kPath, "wb");
    uint8_t h[26] = {0};
    memcpy(h, "C64File", 7);
    fwrite(h, 1, 26, fp);
    fputs("\x01\x08PRG", fp);
    fclose(fp);
    HostFile hf(kPath);
    auto src = hf.getSourceStream(std::ios_base::in);
    P00MFile pf(kPath);
    auto st = std::static_pointer_cast<P00MStream>(pf.getDecodedStream(src));
    TEST_ASSERT_EQUAL_UINT8(0, st->relRecordLength());
    TEST_ASSERT_NULL(st->relStorage().get());
    uint8_t buf[16];
    TEST_ASSERT_EQUAL_UINT32(5, st->read(buf, sizeof(buf)));   // PRG load unchanged
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[0]);
}

// vdrive_rel_open (newrelfile): a new REL file has one record at OPEN, $FF
// then zeros - not zero records - both in the stream and on disk.
void test_new_rel_has_one_record(void)
{
    const uint32_t LEN = 30;
    {
        auto f = make_file();
        auto s = f->createRelStream(LEN);
        TEST_ASSERT_NOT_NULL(s.get());
        auto* rel = static_cast<RELMStream*>(s.get());
        TEST_ASSERT_EQUAL_UINT32(1, rel->recordCount());
        TEST_ASSERT_EQUAL_UINT8(0, rel->position(1, 1));
        uint8_t got[LEN];
        TEST_ASSERT_EQUAL_UINT32(1, rel->read(got, sizeof(got)));   // trailing zeros are not data
        TEST_ASSERT_EQUAL_UINT8(0xFF, got[0]);
        TEST_ASSERT_EQUAL_UINT8(0, rel->status());
        rel->close();
    }
    FILE* fp = fopen(kPath, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    uint8_t b[26 + LEN + 1];
    TEST_ASSERT_EQUAL_UINT32(26 + LEN, (uint32_t)fread(b, 1, sizeof(b), fp));
    fclose(fp);
    TEST_ASSERT_EQUAL_UINT8(0xFF, b[26]);
    for (uint32_t i = 1; i < LEN; i++) TEST_ASSERT_EQUAL_UINT8(0, b[26 + i]);
}

// A REL file on media that cannot be written still opens and reads; only a
// write fails, with 26 (what the drive gets by keeping the read-only stream).
void test_read_only_rel_reads_and_refuses_writes(void)
{
    {
        auto f = make_file();
        auto s = f->createRelStream(30);
        TEST_ASSERT_NOT_NULL(s.get());
        auto* rel = static_cast<RELMStream*>(s.get());
        rel->write((const uint8_t*)"abc", 3);
        rel->endRecord();
        rel->close();
    }
    HostFile hf(kPath);
    auto src = hf.getSourceStream(std::ios_base::in);
    P00MFile pf(kPath);
    auto st = std::static_pointer_cast<P00MStream>(pf.getDecodedStream(src));
    st->mode = std::ios_base::in;
    auto storage = st->relStorage();
    TEST_ASSERT_NOT_NULL(storage.get());
    RELMStream rel(std::move(storage), 30);
    TEST_ASSERT_EQUAL_UINT8(0, rel.position(1, 1));
    uint8_t got[30];
    TEST_ASSERT_EQUAL_UINT32(3, rel.read(got, sizeof(got)));
    TEST_ASSERT_EQUAL_MEMORY("abc", got, 3);
    TEST_ASSERT_EQUAL_UINT8(0, rel.position(1, 1));
    TEST_ASSERT_EQUAL_UINT32(0, rel.write((const uint8_t*)"x", 1));
    TEST_ASSERT_EQUAL_UINT8(26, rel.status());
}

static std::vector<uint8_t> hdr26(const char* sig8, const std::vector<uint8_t>& name)
{
    std::vector<uint8_t> h(26, 0);
    memcpy(h.data(), sig8, 8);
    for (size_t i = 0; i < name.size() && i < 16; i++) h[8 + i] = name[i];
    return h;
}

void test_stream_reports_header_name(void)
{
    auto f = make_file();
    TEST_ASSERT_NOT_NULL(f->createRelStream(30).get());
    auto s = f->getDecodedStream(f->sourceFile->getSourceStream(std::ios_base::in));
    std::string n;
    TEST_ASSERT_TRUE(s->x00Header(n));
    TEST_ASSERT_EQUAL_STRING("scores", n.c_str());
    // a file with no P00 signature is not an x00
    FILE* fp = fopen(kPath, "wb");
    for (int i = 0; i < 40; i++) fputc('z', fp);
    fclose(fp);
    auto s2 = f->getDecodedStream(f->sourceFile->getSourceStream(std::ios_base::in));
    TEST_ASSERT_FALSE(s2->x00Header(n));
}

void test_x00_type_letter(void)
{
    TEST_ASSERT_EQUAL_CHAR('p', x00TypeLetter("game.p00"));
    TEST_ASSERT_EQUAL_CHAR('s', x00TypeLetter("a.S00"));
    TEST_ASSERT_EQUAL_CHAR('u', x00TypeLetter("a.u00"));
    TEST_ASSERT_EQUAL_CHAR('r', x00TypeLetter("scores.R00"));
    TEST_ASSERT_EQUAL_CHAR(0, x00TypeLetter("game.prg"));
    TEST_ASSERT_EQUAL_CHAR(0, x00TypeLetter("game.p01"));
    TEST_ASSERT_EQUAL_CHAR(0, x00TypeLetter("p00"));
    TEST_ASSERT_EQUAL_CHAR(0, x00TypeLetter("x.d64"));
    TEST_ASSERT_EQUAL_STRING("rel", x00TypeName('r'));
    TEST_ASSERT_EQUAL_STRING("usr", x00TypeName('u'));
    // after the IEC-boundary conversion: unshifted $50 $52 $47, not $D0.. graphics
    std::string prg = mstr::toPETSCII2(x00TypeName('p'));
    TEST_ASSERT_EQUAL_UINT32(3, prg.size());
    TEST_ASSERT_EQUAL_UINT8(0x50, (uint8_t)prg[0]);
    TEST_ASSERT_EQUAL_UINT8(0x52, (uint8_t)prg[1]);
    TEST_ASSERT_EQUAL_UINT8(0x47, (uint8_t)prg[2]);
    std::string seq = mstr::toPETSCII2(x00TypeName('s'));
    TEST_ASSERT_EQUAL_UINT8(0x53, (uint8_t)seq[0]);
}

void test_x00_header_name(void)
{
    // PETSCII $53.. = unshifted "SCORES" -> lowercase UTF-8 per repo convention
    auto h = hdr26("C64File\0", {0x53, 0x43, 0x4F, 0x52, 0x45, 0x53});
    std::string n;
    TEST_ASSERT_TRUE(x00HeaderName(h.data(), h.size(), n));
    TEST_ASSERT_EQUAL_STRING("scores", n.c_str());
    // 16-byte name with no terminator
    auto full = hdr26("C64File\0", std::vector<uint8_t>(16, 0x41));
    TEST_ASSERT_TRUE(x00HeaderName(full.data(), full.size(), n));
    TEST_ASSERT_EQUAL_UINT32(16, n.size());
    // bad signature and short buffer are refused
    auto bad = hdr26("C64FilX\0", {0x41});
    TEST_ASSERT_FALSE(x00HeaderName(bad.data(), bad.size(), n));
    TEST_ASSERT_FALSE(x00HeaderName(h.data(), 25, n));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_host_filename_basic);
    RUN_TEST(test_host_filename_replaces_bad_characters);
    RUN_TEST(test_host_filename_empty);
    RUN_TEST(test_p00_rel_round_trip);
    RUN_TEST(test_header_holds_cbm_name_and_flag);
    RUN_TEST(test_existing_file_is_not_clobbered);
    RUN_TEST(test_non_writable_container_gives_write_protect);
    RUN_TEST(test_plain_p00_is_not_rel);
    RUN_TEST(test_new_rel_has_one_record);
    RUN_TEST(test_read_only_rel_reads_and_refuses_writes);
    RUN_TEST(test_stream_reports_header_name);
    RUN_TEST(test_x00_type_letter);
    RUN_TEST(test_x00_header_name);
    return UNITY_END();
}
