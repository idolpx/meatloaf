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

#ifndef GPIBCONFIG_H
#define GPIBCONFIG_H
#include "../../include/pinmap.h"

// un-comment this if you are using open-collector drivers for the CLK/DATA
// lines (e.g. a 74LS07). If so, the GPIBBusHandler constructor requires
// two extra pins for the CLK/DATA output signals
#ifdef GPIB_SPLIT_LINES
#define GPIB_USE_LINE_DRIVERS
#endif

// un-comment this IN ADDITION to USE_LINE_DRIVERS if you are using inverted
// line drivers (such as 74LS06)
#ifdef GPIB_INVERTED_LINES
#define GPIB_USE_INVERTED_LINE_DRIVERS
#endif


// un-comment these #defines to completely disable support for the
// corresponding fast-load protocols (saves program memory in small devices)
#if defined(PIN_XRA1405_CS) && defined(PIN_PARALLEL_PA2) && defined(PIN_PARALLEL_PC2) && defined(PIN_PARALLEL_FLAG2)
#define GPIB_SUPPORT_PARALLEL_XRA1405 // Use XRA1405 port extender for parallel cable
#endif

// defines the maximum number of devices that the bus handler will be
// able to support - set to 4 by default but can be increased to up to 30 devices
#define GPIB_MAX_DEVICES 30


// buffer size for GPIBFileDevice when receiving data. On channel 15, any command
// longer than this (received in a single transaction) will be cut off.
// For other channels, the device's write() function will be called once the
// buffer is full. Every instance of GPIBFileDevice will allocate this buffer
// so it should be kept small on platforms with little RAM (e.g. Arduino UNO)
#define GPIBFILEDEVICE_WRITE_BUFFER_SIZE  255

// buffer size for GPIBFileDevice transmitting data on channel 15, if
// GPIBFileDevice::setStatus() is called with data longer than this it will be clipped.
// every instance of GPIBFileDevice will allocate this buffer so it should be
// kept small on platforms with little RAM (e.g. Arduino UNO)
#define GPIBFILEDEVICE_STATUS_BUFFER_SIZE 255

// convenience macro, GPIB_SUPPORT_PARALLEL is defined if any of the supported
// fast-load protocols use a parallel cable
//#define GPIB_SUPPORT_PARALLEL

#endif
