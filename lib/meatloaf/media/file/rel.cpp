// RELMStream - see rel.h. Behaviour references lib/vdrive/vdrive-rel.c.
#include "media/file/rel.h"

#include <new>

#include "cbm_defines.h"

RELMStream::RELMStream(std::unique_ptr<RELStorage> storage, uint8_t record_length)
    : MStream(""), storage_(std::move(storage)), len_(record_length)
{
    buf_ = new (std::nothrow) uint8_t[len_ ? len_ : 1];
    _size = storage_ ? storage_->size() : 0;
    open_ = true;
}

RELMStream::~RELMStream() { delete[] buf_; }

uint32_t RELMStream::recordCount()
{
    return len_ ? storage_->size() / len_ : 0;
}

void RELMStream::close()
{
    padRecord();
    open_ = false;
}

bool RELMStream::seek(uint32_t pos)
{
    if (!len_) return false;
    padRecord();
    record_ = pos / len_;
    cursor_ = pos % len_;
    _position = pos;
    exhausted_ = false;
    return loadRecord();
}

// vdrive_rel_fillrecord: the rest of a record that was written is zero-filled.
void RELMStream::padRecord()
{
    if (written_ && loaded_ && cursor_ < len_ && record_ < recordCount()) {
        uint32_t n = len_ - cursor_;
        for (uint32_t i = cursor_; i < len_; i++) buf_[i] = 0;
        storage_->writeAt(record_ * len_ + cursor_, buf_ + cursor_, n);
        cursor_ = len_;
    }
    written_ = false;
}

bool RELMStream::loadRecord()
{
    loaded_ = false;
    if (!buf_ || record_ >= recordCount()) return false;
    if (storage_->readAt(record_ * len_, buf_, len_) != len_) return false;
    loaded_ = true;
    computeReadEnd();
    return true;
}

// vdrive_rel_position_internal: scan back from the record end to the last
// non-zero byte. A record that is all zeros still yields one byte (VICE reads
// buffer[bufptr] before testing for the end). A cursor past the last non-zero
// byte reads a single byte (the 1541's "length 1" case; the same-sector
// 255-position overflow bug is not emulated).
void RELMStream::computeReadEnd()
{
    uint32_t e = len_;
    while (e > cursor_ && buf_[e - 1] == 0) e--;
    if (e <= cursor_) e = cursor_ + 1;
    read_end_ = e;
}

// vdrive_rel_position_internal
uint8_t RELMStream::position(uint16_t record, uint8_t pos)
{
    padRecord();
    exhausted_ = false;
    uint32_t p = pos == 0 ? 0 : pos - 1;
    if (p >= len_) return status_ = ST_RECORD_OVERFLOW;
    uint32_t r = record == 0 ? 0 : record - 1;
    record_ = r;
    cursor_ = p;
    loaded_ = false;
    if (r >= recordCount()) {
        // a freshly created REL reports OK on record 1
        return status_ = (r > 0 ? ST_RECORD_MISSING : ST_OK);
    }
    if (!loadRecord()) return status_ = ST_RECORD_MISSING;
    return status_ = ST_OK;
}

// vdrive_rel_read
uint32_t RELMStream::read(uint8_t* out, uint32_t size)
{
    if (!out || !size) return 0;
    if (exhausted_) {
        // end-of-record marker delivered; the next read continues in the next record
        exhausted_ = false;
        record_++;
        cursor_ = 0;
        loaded_ = false;
        return 0;
    }
    if (record_ >= recordCount()) {
        out[0] = 0x0D;
        status_ = ST_RECORD_MISSING;
        return 1;
    }
    if (!loaded_ && !loadRecord()) {
        out[0] = 0x0D;
        status_ = ST_RECORD_MISSING;
        return 1;
    }
    uint32_t n = read_end_ - cursor_;
    if (n > size) n = size;
    for (uint32_t i = 0; i < n; i++) out[i] = buf_[cursor_ + i];
    cursor_ += n;
    _position += n;
    if (cursor_ >= read_end_) exhausted_ = true;
    return n;
}

// vdrive_rel_grow: new records are $FF followed by zeros.
uint8_t RELMStream::growTo(uint32_t records)
{
    uint32_t old = recordCount();
    uint8_t st = storage_->grow(records * len_);
    if (st != ST_OK) return st;
    for (uint32_t r = old; r < records; r++) {
        uint32_t base = r * len_;
        uint8_t b = 0xFF;
        storage_->writeAt(base, &b, 1);
        b = 0;
        for (uint32_t i = 1; i < len_; i++) storage_->writeAt(base + i, &b, 1);
    }
    return ST_OK;
}

// vdrive_rel_write
uint32_t RELMStream::write(const uint8_t* in, uint32_t size)
{
    if (!in || !size) return 0;
    if (!storage_->writable()) {
        status_ = ST_WRITE_PROTECT_ON;
        return 0;
    }
    if (record_ >= recordCount()) {
        uint8_t st = growTo(record_ + 1);
        if (st != ST_OK) {
            status_ = st;
            return 0;
        }
    }
    if (!loaded_ && !loadRecord()) {
        status_ = ST_RECORD_MISSING;
        return 0;
    }
    uint32_t room = cursor_ < len_ ? len_ - cursor_ : 0;
    uint32_t n = size < room ? size : room;
    if (n) {
        for (uint32_t i = 0; i < n; i++) buf_[cursor_ + i] = in[i];
        storage_->writeAt(record_ * len_ + cursor_, buf_ + cursor_, n);
        cursor_ += n;
        written_ = true;
        exhausted_ = false;
    }
    if (n < size) {
        // never write into the next record; still "ok" to the caller, but with a DOS error
        status_ = ST_RECORD_OVERFLOW;
    }
    return size;
}

// End of a PRINT# line: pad the record with zeros and move to the next one.
void RELMStream::endRecord()
{
    padRecord();
    record_++;
    cursor_ = 0;
    loaded_ = false;
    exhausted_ = false;
}
