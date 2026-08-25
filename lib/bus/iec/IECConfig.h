// -----------------------------------------------------------------------------
// Copyright (C) 2024 David Hansel
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------

#ifndef IECCONFIG_H
#define IECCONFIG_H
#include "../../include/pinmap.h"

// un-comment this if you are using open-collector drivers for the CLK/DATA
// lines (e.g. a 7406 or 7407). If so, the IECBusHandler constructor requires
// two extra pins for the CLK/DATA output signals
#ifdef IEC_SPLIT_LINES
#define IEC_USE_LINE_DRIVERS
#endif

// un-comment this IN ADDITION to IEC_USE_LINE_DRIVERS if you are using inverted
// line drivers (such as 7406)
#ifdef IEC_INVERTED_LINES
#define IEC_USE_INVERTED_LINE_DRIVERS
#endif

// un-comment this if you are using inverters on the IEC bus input signals
// (only has an effect if IEC_USE_LINE_DRIVERS is also enabled)
//#define IEC_USE_INVERTED_INPUTS

// comment out these #defines to completely disable support for the
// corresponding fast-load protocols (saves program memory in small devices)
// Hardware fast loaders. These require specific hardware on the host and/or the drive.
// Some upload 6502 code into the drive RAM like the software fast loaders.
#define IEC_FP_FAST_SERIAL  0  // CBM Fast Serial
#define IEC_FP_JIFFY        1  // JiffyDos
#define IEC_FP_EPYX         2  // EPYX FastLoad
#define IEC_FP_FC3          3  // Final Cartridge 3
#define IEC_FP_AR6          4  // Action Replay 6

// Parallel fast loaders.
#if defined(PIN_PARALLEL_PC2) && defined(PIN_PARALLEL_FLAG2)
#define IEC_IEEE_488        5  // IEEE 488
#define IEC_FP_DOLPHIN      6  // Dolphin Dos
#define IEC_FP_SPEEDDOS     7  // Speed Dos
#ifdef PIN_PARALLEL_PA2
#define IEC_FP_WIC64        8  // WiC64 Protocol Available
#endif
#endif

// Software fast loaders. These upload 6502 code into drive RAM with M-W and
// then start it with M-E, so they need no extra wiring -- unlike the parallel
// loaders above they are available on every board. Detection is by CRC of the
// uploaded bytes, see fastload.h.
#define IEC_FP_HYPRALOAD    9  // Hypra-Load (64er Magazin)
#define IEC_FP_TURBODISK    10 // Turbodisk
#define IEC_FP_DREAMLOAD    11 // Dreamload
#define IEC_FP_ULOAD3       12 // ULoad Model 3
#define IEC_FP_GIJOE        13 // GI Joe
#define IEC_FP_GEOS         14 // GEOS
#define IEC_FP_WHEELS       15 // Wheels (needs IEC_FP_GEOS)
#define IEC_FP_NIPPON       16 // Nippon
#define IEC_FP_ELOAD1       17 // ELoad version 1
#define IEC_FP_MMZAK        18 // Maniac Mansion / Zak McKracken
#define IEC_FP_N0SDOS       19 // N0SDOS file read
#define IEC_FP_SAMSJOURNEY  20 // Sam's Journey
#define IEC_FP_ULTRABOOT    21 // Ultraboot
#define IEC_FP_KRILL        22 // Krill's loader (r58 through r192)
#define IEC_FP_BOOZE        23 // Booze Design
#define IEC_FP_SPINDLE      24 // Spindle 2.1 and later
#define IEC_FP_BITFIRE      25 // Bitfire 0.1 through 1.3
#define IEC_FP_SPARKLE      26 // Sparkle 1.0 through 3.2
#define IEC_FP_TRANSWARP    27 // Transwarp

// A session-owning software loader takes the bus after its M-E and serves the
// whole transfer itself, so each one is a few KB of flash text. An ESP32 with
// the small ~3.3 MB iram0_2_seg window (lolin-d32-pro and its family) does not
// have room for all of them -- the same budget that gates EXTRA_DISK_FORMATS.
//
// DETECTION is always compiled: it is two tables and costs almost nothing. A
// board without the implementation still recognises the loader, logs it, and
// returns false from startFastLoader(), which lets the M-E through so the
// computer falls back to the standard protocol rather than hanging. Define
// EXTRA_FASTLOADERS in a board's build_flags to compile the transfer code.
#ifdef EXTRA_FASTLOADERS
#define IEC_IMPL_SOFTLOAD
#endif

// Wheels is an extension of the GEOS loader and shares its transfer routines
#if defined(IEC_FP_WHEELS) && !defined(IEC_FP_GEOS)
#undef IEC_FP_WHEELS
#endif


// convenience macro, IEC_SUPPORT_FASTLOAD is defined if any fast-load protocols
// are enabled
#if defined(IEC_FP_JIFFY) || defined(IEC_FP_EPYX) || defined(IEC_FP_FC3) || defined(IEC_FP_AR6) || defined(IEC_FP_DOLPHIN) || defined(IEC_FP_SPEEDDOS) || defined(IEC_FP_HYPRALOAD) || \
    defined(IEC_FP_TURBODISK) || defined(IEC_FP_DREAMLOAD) || defined(IEC_FP_ULOAD3) || defined(IEC_FP_GIJOE) || defined(IEC_FP_GEOS) || defined(IEC_FP_NIPPON) || defined(IEC_FP_ELOAD1) || defined(IEC_FP_MMZAK) || defined(IEC_FP_N0SDOS) || defined(IEC_FP_SAMSJOURNEY) || defined(IEC_FP_ULTRABOOT) || defined(IEC_FP_KRILL) || defined(IEC_FP_BOOZE) || defined(IEC_FP_SPINDLE) || defined(IEC_FP_BITFIRE) || defined(IEC_FP_SPARKLE) || defined(IEC_FP_TRANSWARP)
#define IEC_SUPPORT_FASTLOAD
#endif

// convenience macro, IEC_SUPPORT_SOFTLOAD is defined if any fast loader that is
// detected from uploaded drive code is enabled. Those loaders need the CRC
// tracking in driveMemory and the detection tables in fastload.cpp.
#if defined(IEC_FP_TURBODISK) || defined(IEC_FP_DREAMLOAD) || defined(IEC_FP_ULOAD3) || defined(IEC_FP_GIJOE) || \
    defined(IEC_FP_GEOS) || defined(IEC_FP_NIPPON) || defined(IEC_FP_ELOAD1) || defined(IEC_FP_MMZAK) || defined(IEC_FP_N0SDOS) || defined(IEC_FP_SAMSJOURNEY) || defined(IEC_FP_FC3) || defined(IEC_FP_AR6) || defined(IEC_FP_EPYX) || defined(IEC_FP_ULTRABOOT) || defined(IEC_FP_KRILL) || defined(IEC_FP_BOOZE) || defined(IEC_FP_SPINDLE) || defined(IEC_FP_BITFIRE) || defined(IEC_FP_SPARKLE) || defined(IEC_FP_TRANSWARP)
#define IEC_SUPPORT_SOFTLOAD
#endif

// un-comment this to use a XRA1405 port expander for the 8-bit parallel cable
// instead of connecting the parallel pins directly to the microcontroller
#ifdef PIN_XRA1405_CS
#define IEC_SUPPORT_PARALLEL_XRA1405
#endif

// support Epyx FastLoad sector operations (disk editor, disk copy, file copy)
// if this is enabled then the buffer in the setBuffer() call must have a size of
// at least 256 bytes. Note that the "bufferSize" argument is a byte and therefore
// capped at 255 bytes. Make sure the buffer itself has >=256 bytes and use a 
// bufferSize argument of 255 or less
#define IEC_FP_EPYX_SECTOROPS

// convenience macro, IEC_SUPPORT_SECTOROPS is defined if any enabled loader
// asks the device for raw track/sector access rather than for a file. The
// hooks are still called epyxReadSector/epyxWriteSector because Epyx was the
// first loader to need them, but they are not Epyx-specific.
#if (defined(IEC_FP_EPYX) && defined(IEC_FP_EPYX_SECTOROPS)) || (defined(IEC_IMPL_SOFTLOAD) && (defined(IEC_FP_NIPPON) || defined(IEC_FP_ULOAD3) || defined(IEC_FP_MMZAK) || defined(IEC_FP_GEOS) || defined(IEC_FP_DREAMLOAD) || defined(IEC_FP_BOOZE) || defined(IEC_FP_ULTRABOOT) || defined(IEC_FP_BITFIRE) || defined(IEC_FP_SPINDLE) || defined(IEC_FP_SPARKLE) || defined(IEC_FP_KRILL)))
#define IEC_SUPPORT_SECTOROPS
#endif

// Drive types an image can represent, as reported by IECDevice::imageType().
// A loader that walks the disk itself often refuses anything but a 1541.
#define IEC_IMG_1541 0
#define IEC_IMG_1571 1
#define IEC_IMG_1581 2
#define IEC_IMG_NONE 10

// defines the maximum number of devices that the bus handler will be
// able to support - set to 4 by default but can be increased to up to 30 devices
#define IEC_MAX_DEVICES 30

// sets the default size of the fastload buffer. If this is set to 0 then fastload
// protocols can only be used if the IECBusHandler::setBuffer() function is
// called to define the buffer. 128 should be a good value, larger values have
// little effect on transmission speed. Values are capped at 254.
#if defined(IEC_SUPPORT_FASTLOAD)
#define IEC_DEFAULT_FASTLOAD_BUFFER_SIZE 128
#endif

// buffer size for IECFileDevice when receiving data. On channel 15, any command
// longer than this (received in a single transaction) will be cut off.
// For other channels, the device's write() function will be called once the
// buffer is full. Every instance of IECFileDevice will allocate this buffer
// so it should be kept small on platforms with little RAM (e.g. Arduino UNO)
#define IECFILEDEVICE_WRITE_BUFFER_SIZE  255

// buffer size for IECFileDevice transmitting data on channel 15, if
// IECFileDevice::setStatus() is called with data longer than this it will be clipped.
// every instance of IECFileDevice will allocate this buffer so it should be
// kept small on platforms with little RAM (e.g. Arduino UNO)
#define IECFILEDEVICE_STATUS_BUFFER_SIZE 255

// convenience macro, IEC_SUPPORT_PARALLEL is defined if any of the supported
// fast-load protocols use a parallel cable
#if defined(IEC_FP_DOLPHIN) || defined(IEC_FP_SPEEDDOS)
#define IEC_SUPPORT_PARALLEL
#endif

#endif /* IECCONFIG_H */
