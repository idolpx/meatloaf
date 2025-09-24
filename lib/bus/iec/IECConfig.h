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
// lines (e.g. a 74LS07). If so, the IECBusHandler constructor requires
// two extra pins for the CLK/DATA output signals
#ifdef IEC_SPLIT_LINES
#define IEC_USE_LINE_DRIVERS
#endif

// un-comment this IN ADDITION to USE_LINE_DRIVERS if you are using inverted
// line drivers (such as 74LS06)
#ifdef IEC_INVERTED_LINES
#define IEC_USE_INVERTED_LINE_DRIVERS
#endif

// un-comment these #defines to completely disable support for the
// corresponding fast-load protocols (saves program memory in small devices)
#define IEC_FP_JIFFY     0 // JiffyDos
#define IEC_FP_EPYX      1 // EPYX FastLoad
#define IEC_FP_FC3       2 // Final Cartridge 3
#define IEC_FP_AR6       3 // Action Replay 6
#if defined(PIN_PARALLEL_PC2) && defined(PIN_PARALLEL_FLAG2)
#define IEC_FP_DOLPHIN   4 // Dolphin Dos
#define IEC_FP_SPEEDDOS  5 // Speed Dos
#ifdef PIN_PARALLEL_PA2
#define IEC_FP_WIC64     6 // WiC64 Protocol Available
#endif
#ifdef PIN_XRA1405_CS
#define IEC_SUPPORT_PARALLEL_XRA1405 // Use XRA1405 port extender for parallel cable
#endif
#endif

// convenience macro, IEC_SUPPORT_FASTLOAD is defined if any fast-load protocols
// are enabled
#if defined(IEC_FP_JIFFY) || defined(IEC_FP_EPYX) || defined(IEC_FP_FC3) || defined(IEC_FP_AR6) || defined(IEC_FP_DOLPHIN) || defined(IEC_FP_SPEEDDOS)
#define IEC_SUPPORT_FASTLOAD
#endif

// support Epyx FastLoad sector operations (disk editor, disk copy, file copy)
// if this is enabled then the buffer in the setBuffer() call must have a size of
// at least 256 bytes. Note that the "bufferSize" argument is a byte and therefore
// capped at 255 bytes. Make sure the buffer itself has >=256 bytes and use a 
// bufferSize argument of 255 or less
#define IEC_FP_EPYX_SECTOROPS

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

#endif
