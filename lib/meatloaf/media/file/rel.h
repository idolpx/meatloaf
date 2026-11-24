// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

// .REL - RELative file layout
//
// https://ist.uwaterloo.ca/~schepers/formats/REL.TXT
// https://www.atarimagazines.com/compute/issue40/relative_files.php
//

// Media-independent record layer for CBM relative files. A RELStorage hides
// where the record bytes live (D64-family side sectors, a P00 .r00 file, ...);
// RELMStream adds CBM record semantics on top. Behaviour follows VICE's
// lib/vdrive/vdrive-rel.c.

#ifndef MEATLOAF_MEDIA_REL_H
#define MEATLOAF_MEDIA_REL_H

#include <cstdint>
#include <memory>

#include "meatloaf.h"

class RELStorage {
public:
    virtual ~RELStorage() = default;
    virtual uint32_t size() = 0;                                        // bytes of record data
    virtual uint32_t readAt(uint32_t off, uint8_t* buf, uint32_t n) = 0;
    virtual uint32_t writeAt(uint32_t off, const uint8_t* buf, uint32_t n) = 0;
    virtual uint8_t  grow(uint32_t new_size) = 0;   // ST_OK, or the CBM status (52/72/26); content of new bytes unspecified
    virtual bool     writable() = 0;
};

class RELMStream : public MStream {
public:
    RELMStream(std::unique_ptr<RELStorage> storage, uint8_t record_length);
    ~RELMStream() override;

    uint8_t  recordLength() const { return len_; }
    uint32_t recordCount();                                  // storage size / record length
    uint8_t  position(uint16_t record, uint8_t pos);         // CBM P: 1-based, 0==1; returns ST_OK / 50 / 51
    uint32_t read(uint8_t* buf, uint32_t size) override;     // current record only; 0 = record end
    uint32_t write(const uint8_t* buf, uint32_t size) override;
    void     endRecord();                                    // end of a PRINT#: pad, advance
    bool     recordExhausted() const { return exhausted_; }
    uint8_t  status() const { return status_; }              // last CBM status

    bool isOpen() override { return open_; }
    bool open(std::ios_base::openmode m) override { mode = m; open_ = true; return true; }
    void close() override;
    bool seek(uint32_t pos) override;   // raw byte offset into the record data

private:
    bool loadRecord();                  // buffer_ <- current record; false if absent
    void computeReadEnd();              // trailing-zero scan
    void padRecord();                   // zero-fill rest of a written record
    uint8_t growTo(uint32_t records);   // grow storage; new records = $FF,0...

    std::unique_ptr<RELStorage> storage_;
    uint8_t* buf_ = nullptr;            // one record, heap
    uint8_t  len_;
    uint32_t record_ = 0;               // 0-based current record
    uint32_t cursor_ = 0;               // offset within the record
    uint32_t read_end_ = 0;             // readable bytes in the record
    bool     loaded_ = false;
    bool     exhausted_ = false;
    bool     written_ = false;
    bool     open_ = false;
    uint8_t  status_ = 0;
};

#endif
