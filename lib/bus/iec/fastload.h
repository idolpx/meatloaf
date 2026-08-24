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
//
// -----------------------------------------------------------------------------
// Detection of software fast loaders.
//
// A software fast loader uploads 6502 code into drive RAM with a run of M-W
// commands and then starts it with M-E. Neither command says which loader it
// is, so it is identified the way sd2iec identifies it: by a CRC-16 running
// over every uploaded data byte, looked up in a table of known values, and
// then confirmed by the address the following M-E jumps to.
//
// Two id spaces meet here and they are not the same thing:
//
//   family  - IEC_FP_* in IECConfig.h. This is the bit index in the
//             enable/disable mask, so it names a loader the user can turn on.
//   variant - IEC_FLV_* below. This names one concrete uploaded routine.
//             Several variants share a family: FC3 has load, save and freezed
//             variants, GEOS has one per drive type and version.
//
// The variant numbering mirrors sd2iec's FL_* so its tables transcribe
// directly and stay comparable with upstream.
// -----------------------------------------------------------------------------

#ifndef IEC_FASTLOAD_H
#define IEC_FASTLOAD_H

#include "IECConfig.h"
#include <stdint.h>
#include <stddef.h>

#define IEC_FLV_NONE              0
#define IEC_FLV_DREAMLOAD         1
#define IEC_FLV_DREAMLOAD_OLD     2
#define IEC_FLV_TURBODISK         3
#define IEC_FLV_FC3_LOAD          4
#define IEC_FLV_FC3_SAVE          5
#define IEC_FLV_FC3_FREEZED       6
#define IEC_FLV_ULOAD3            7
#define IEC_FLV_GI_JOE            8
#define IEC_FLV_EPYXCART          9
#define IEC_FLV_GEOS_S1_64       10
#define IEC_FLV_GEOS_S1_128      11
#define IEC_FLV_GEOS_S23_1541    12
#define IEC_FLV_GEOS_S23_1571    13
#define IEC_FLV_GEOS_S23_1581    14
#define IEC_FLV_WHEELS_S1_64     15
#define IEC_FLV_WHEELS_S1_128    16
#define IEC_FLV_WHEELS_S2        17
#define IEC_FLV_WHEELS44_S2      18
#define IEC_FLV_WHEELS44_S2_1581 19
#define IEC_FLV_NIPPON           20
#define IEC_FLV_AR6_1581_LOAD    21
#define IEC_FLV_AR6_1581_SAVE    22
#define IEC_FLV_ELOAD1           23
#define IEC_FLV_FC3_OLDFREEZED   24
#define IEC_FLV_MMZAK            25
#define IEC_FLV_N0SDOS_FILEREAD  26
#define IEC_FLV_SAMSJOURNEY      27

// Byte transfer timing. Which pair applies is a property of the uploaded code,
// so it comes out of the CRC table rather than being decided by the handler.
#define IEC_FLRX_NONE            0
#define IEC_FLRX_GEOS_1MHZ       1
#define IEC_FLRX_GEOS_2MHZ       2
#define IEC_FLRX_GEOS_1581_21    3
#define IEC_FLRX_WHEELS_1MHZ     4
#define IEC_FLRX_WHEELS_2MHZ     5
#define IEC_FLRX_WHEELS44_1541   6
#define IEC_FLRX_WHEELS44_1581   7
#define IEC_FLRX_FC3OF_PAL       8
#define IEC_FLRX_FC3OF_NTSC      9

// CRC-16 with the reflected polynomial 0xA001, initial value 0xFFFF -- the
// routine AVR libc calls _crc16_update and sd2iec calls crc16_update. Its
// published table constants only reproduce with this polynomial; none of the
// esp_rom_crc16_* variants (0x1021 based) will do.
uint16_t iecCrc16Update(uint16_t crc, uint8_t data);
uint16_t iecCrc16Block(uint16_t crc, const uint8_t *data, size_t len);

// The family (IEC_FP_*) a variant belongs to, or 0xFF if the variant is not
// compiled in.
uint8_t iecFastLoadFamily(uint8_t variant);

// Human-readable name of a variant, for logging. Never NULL.
const char *iecFastLoadName(uint8_t variant);

// Per-drive detection state. One instance per drive: each drive has its own
// RAM and receives its own uploads.
class IECFastLoadDetect
{
 public:
  // Feed the data bytes of one M-W. Addresses that carry no loader code are
  // skipped exactly as sd2iec skips them -- see the .cpp. Returns the variant
  // detected so far, IEC_FLV_NONE if nothing matches yet.
  //
  // The lookup runs after every M-W, not only at M-E, because a loader is
  // recognisable part way through its upload while later blocks keep adding
  // to the CRC.
  uint8_t memWrite(uint16_t address, const uint8_t *data, size_t len);

  // Resolve an M-E. Returns the variant to run and writes its handler
  // parameter to *param, or IEC_FLV_NONE if this address belongs to no known
  // loader. Rolls the detection state either way, so call it for every M-E.
  uint8_t memExec(uint16_t address, uint8_t *param);

  // Called when the drive is reset or initialised.
  void reset();

  uint16_t crc() const { return m_crc; }
  uint8_t detected() const { return m_detected; }
  uint8_t rxtx() const { return m_rxtx; }

 private:
  uint16_t m_crc = 0xFFFF;
  uint8_t m_detected = IEC_FLV_NONE;
  uint8_t m_previous = IEC_FLV_NONE;
  uint8_t m_rxtx = IEC_FLRX_NONE;
};

#endif
