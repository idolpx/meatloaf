// RELMStream: the media-independent record layer. Each test names the VICE
// function (lib/vdrive/vdrive-rel.c) whose observable behaviour it pins.
#include <unity.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "cbm_defines.h"
#include "meatloaf.h"
#include "media/file/rel.h"

static const uint8_t LEN = 10;

// Backing store shared with the test so it can inspect bytes after the stream
// owns the storage.
struct MemData {
    std::vector<uint8_t> bytes;
    uint8_t grow_status = ST_OK;   // what grow() answers
    bool    can_write = true;
    uint32_t writes = 0;           // writeAt() calls
};

class MemRELStorage : public RELStorage {
public:
    explicit MemRELStorage(MemData* d) : d_(d) {}
    uint32_t size() override { return (uint32_t)d_->bytes.size(); }
    uint32_t readAt(uint32_t off, uint8_t* buf, uint32_t n) override {
        if (off >= size()) return 0;
        if (n > size() - off) n = size() - off;
        for (uint32_t i = 0; i < n; i++) buf[i] = d_->bytes[off + i];
        return n;
    }
    uint32_t writeAt(uint32_t off, const uint8_t* buf, uint32_t n) override {
        d_->writes++;
        if (off >= size()) return 0;
        if (n > size() - off) n = size() - off;
        for (uint32_t i = 0; i < n; i++) d_->bytes[off + i] = buf[i];
        return n;
    }
    uint8_t grow(uint32_t new_size) override {
        if (d_->grow_status != ST_OK) return d_->grow_status;
        d_->bytes.resize(new_size, 0xEE);   // new content unspecified on purpose
        return ST_OK;
    }
    bool writable() override { return d_->can_write; }
private:
    MemData* d_;
};

static MemData data;
static std::unique_ptr<RELMStream> make(uint32_t records) {
    data = MemData();
    data.bytes.assign(records * LEN, 0);
    return std::unique_ptr<RELMStream>(new RELMStream(std::unique_ptr<RELStorage>(new MemRELStorage(&data)), LEN));
}
static void put(uint32_t rec, const char* s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) data.bytes[rec * LEN + i] = (uint8_t)s[i];
}

void setUp(void) {}
void tearDown(void) {}

// vdrive_rel_position_internal: "position 0 and 1 are the same", record likewise.
void test_position_zero_equals_one() {
    auto s = make(3);
    put(0, "AB", 2);
    uint8_t b[4];
    TEST_ASSERT_EQUAL(ST_OK, s->position(0, 0));
    TEST_ASSERT_EQUAL(2, s->read(b, 4));
    TEST_ASSERT_EQUAL('A', b[0]);
    TEST_ASSERT_EQUAL(ST_OK, s->position(1, 1));
    TEST_ASSERT_EQUAL(2, s->read(b, 4));
    TEST_ASSERT_EQUAL('A', b[0]);
}

// vdrive_rel_position_internal: position >= rec_len -> CBMDOS_IPE_OVERFLOW (51).
void test_position_beyond_record_is_51() {
    auto s = make(3);
    TEST_ASSERT_EQUAL(ST_RECORD_OVERFLOW, s->position(1, LEN + 1));
    TEST_ASSERT_EQUAL(ST_OK, s->position(1, LEN));
}

// vdrive_rel_position_internal: record >= record_max (and > 0) -> 50;
// vdrive_rel_write grows; vdrive_rel_fillrecord/grow fill new records $FF,0...
void test_position_past_end_is_50_then_write_grows() {
    auto s = make(2);
    TEST_ASSERT_EQUAL(ST_RECORD_MISSING, s->position(5, 1));
    TEST_ASSERT_EQUAL(2, s->recordCount());
    const uint8_t w[2] = {'X', 'Y'};
    TEST_ASSERT_EQUAL(2, s->write(w, 2));
    s->endRecord();
    TEST_ASSERT_EQUAL(5, s->recordCount());
    // records 3 and 4 (0-based 2,3) are untouched fresh records: $FF then zeros
    for (int r = 2; r < 4; r++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, data.bytes[r * LEN]);
        for (int i = 1; i < LEN; i++) TEST_ASSERT_EQUAL_HEX8(0, data.bytes[r * LEN + i]);
    }
    // record 5 (0-based 4): XY then zero padding from endRecord
    TEST_ASSERT_EQUAL('X', data.bytes[4 * LEN]);
    TEST_ASSERT_EQUAL('Y', data.bytes[4 * LEN + 1]);
    for (int i = 2; i < LEN; i++) TEST_ASSERT_EQUAL_HEX8(0, data.bytes[4 * LEN + i]);
}

// vdrive_rel_position_internal: a just-created REL reports OK on record 1.
void test_empty_file_position_one_is_ok() {
    auto s = make(0);
    TEST_ASSERT_EQUAL(ST_OK, s->position(1, 1));
    TEST_ASSERT_EQUAL(ST_RECORD_MISSING, s->position(2, 1));
}

// vdrive_rel_position_internal: trailing-zero scan sets the readable length.
void test_read_stops_at_trailing_zeros() {
    auto s = make(2);
    put(0, "AB", 2);
    uint8_t b[LEN];
    s->position(1, 1);
    TEST_ASSERT_EQUAL(2, s->read(b, LEN));
    TEST_ASSERT_EQUAL('A', b[0]);
    TEST_ASSERT_EQUAL('B', b[1]);
    TEST_ASSERT_TRUE(s->recordExhausted());
    TEST_ASSERT_EQUAL(0, s->read(b, LEN));
}

// vdrive_rel_read: after the record-end marker the next read continues in the
// next record (record++ at end of record).
void test_read_continues_in_next_record_after_end() {
    auto s = make(2);
    put(0, "AB", 2);
    put(1, "CD", 2);
    uint8_t b[LEN];
    s->position(1, 1);
    s->read(b, LEN);
    TEST_ASSERT_EQUAL(0, s->read(b, LEN));
    TEST_ASSERT_EQUAL(2, s->read(b, LEN));
    TEST_ASSERT_EQUAL('C', b[0]);
}

// vdrive_rel_read: record >= record_max -> data 0x0d and error 50.
void test_read_past_last_record_gives_cr_and_50() {
    auto s = make(1);
    uint8_t b[4] = {0};
    s->position(1, 1);
    s->read(b, 4);          // record data
    s->read(b, 4);          // end marker
    TEST_ASSERT_EQUAL(1, s->read(b, 4));
    TEST_ASSERT_EQUAL_HEX8(0x0D, b[0]);
    TEST_ASSERT_EQUAL(ST_RECORD_MISSING, s->status());
}

// vdrive_rel_write: bufptr >= record_next -> OVERFLOW (51), data not written.
void test_write_longer_than_record_is_51() {
    auto s = make(2);
    uint8_t w[LEN + 3];
    for (unsigned i = 0; i < sizeof w; i++) w[i] = 'a' + i;
    s->position(1, 1);
    s->write(w, sizeof w);
    TEST_ASSERT_EQUAL(ST_RECORD_OVERFLOW, s->status());
    for (int i = 0; i < LEN; i++) TEST_ASSERT_EQUAL_HEX8('a' + i, data.bytes[i]);
    // the next record must be untouched
    for (int i = 0; i < LEN; i++) TEST_ASSERT_EQUAL_HEX8(0, data.bytes[LEN + i]);
}

// vdrive_rel_fillrecord: rest of a written record is zero-filled; next write
// lands in the next record.
void test_end_record_pads_and_advances() {
    auto s = make(2);
    for (auto& b : data.bytes) b = 0x77;
    s->position(1, 1);
    const uint8_t w[2] = {'H', 'I'};
    s->write(w, 2);
    s->endRecord();
    TEST_ASSERT_EQUAL('H', data.bytes[0]);
    TEST_ASSERT_EQUAL('I', data.bytes[1]);
    for (int i = 2; i < LEN; i++) TEST_ASSERT_EQUAL_HEX8(0, data.bytes[i]);
    const uint8_t w2[1] = {'Z'};
    s->write(w2, 1);
    TEST_ASSERT_EQUAL('Z', data.bytes[LEN]);
}

// vdrive_rel_write: a failed vdrive_rel_grow (72 disk full) writes nothing and
// the record count is unchanged.
void test_grow_failure_leaves_count() {
    auto s = make(1);
    data.grow_status = ST_DISK_FULL;
    s->position(4, 1);
    const uint8_t w[1] = {'Q'};
    TEST_ASSERT_EQUAL(0, s->write(w, 1));
    TEST_ASSERT_EQUAL(ST_DISK_FULL, s->status());
    TEST_ASSERT_EQUAL(1, s->recordCount());
}

// vdrive_rel_write: write-protected media -> 26, nothing written.
void test_write_protect() {
    auto s = make(1);
    data.can_write = false;
    s->position(1, 1);
    const uint8_t w[1] = {'Q'};
    TEST_ASSERT_EQUAL(0, s->write(w, 1));
    TEST_ASSERT_EQUAL(ST_WRITE_PROTECT_ON, s->status());
    TEST_ASSERT_EQUAL_HEX8(0, data.bytes[0]);
}

// Review fix: read after write in the same record must not underflow
// read_end_ - cursor_ nor return bytes beyond the record (vdrive_rel_read
// ends a record at record_next).
void test_read_after_partial_write_stays_in_record() {
    auto s = make(2);
    put(0, "AB", 2);
    put(1, "ZZ", 2);
    uint8_t b[64];
    s->position(1, 1);
    s->read(b, 1);                      // load record, read 'A'
    const uint8_t w[5] = {'a', 'b', 'c', 'd', 'e'};
    s->position(1, 1);
    s->write(w, 5);
    uint32_t n = s->read(b, sizeof b);
    TEST_ASSERT_TRUE(n <= (uint32_t)(LEN - 5));
}

void test_read_after_full_length_write_is_record_end() {
    auto s = make(2);
    put(0, "AB", 2);
    put(1, "ZZ", 2);
    uint8_t b[64];
    uint8_t w[LEN];
    for (int i = 0; i < LEN; i++) w[i] = 'a' + i;
    s->position(1, 1);
    s->write(w, LEN);
    TEST_ASSERT_EQUAL(0, s->read(b, sizeof b));
    // continues in the next record afterwards, not past the buffer
    TEST_ASSERT_EQUAL(2, s->read(b, sizeof b));
    TEST_ASSERT_EQUAL('Z', b[0]);
}

// vdrive_rel_grow fills new records $FF,0... A storage over a disk image
// pays a sector read-modify-write per writeAt(), so the fill goes out one
// record at a time, not one byte at a time.
void test_grow_fill_is_one_write_per_record() {
    auto r = make(0);
    TEST_ASSERT_EQUAL_UINT8(ST_RECORD_MISSING, r->position(3, 1));
    data.writes = 0;
    TEST_ASSERT_EQUAL_UINT32(1, r->write((const uint8_t*)"A", 1));
    // three filled records plus the one byte written
    TEST_ASSERT_EQUAL_UINT32(4, data.writes);
    TEST_ASSERT_EQUAL_UINT8(0xFF, data.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, data.bytes[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, data.bytes[LEN]);
    TEST_ASSERT_EQUAL_UINT8('A', data.bytes[2 * LEN]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_position_zero_equals_one);
    RUN_TEST(test_position_beyond_record_is_51);
    RUN_TEST(test_position_past_end_is_50_then_write_grows);
    RUN_TEST(test_empty_file_position_one_is_ok);
    RUN_TEST(test_read_stops_at_trailing_zeros);
    RUN_TEST(test_read_continues_in_next_record_after_end);
    RUN_TEST(test_read_past_last_record_gives_cr_and_50);
    RUN_TEST(test_write_longer_than_record_is_51);
    RUN_TEST(test_end_record_pads_and_advances);
    RUN_TEST(test_grow_failure_leaves_count);
    RUN_TEST(test_write_protect);
    RUN_TEST(test_read_after_partial_write_stays_in_record);
    RUN_TEST(test_read_after_full_length_write_is_record_end);
    RUN_TEST(test_grow_fill_is_one_write_per_record);
    return UNITY_END();
}
