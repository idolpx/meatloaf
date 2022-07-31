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

#ifndef GLOBAL_DEFINES_H
#define GLOBAL_DEFINES_H

#include "version.h"
#include "ssid.h"
#include "ansi_codes.h"

#define PRODUCT_ID "MEATLOAF CBM"
//#define FW_VERSION "20220422.1" // Dynamically set at compile time in "platformio.ini"

#define USER_AGENT PRODUCT_ID " [" FW_VERSION "]"
//#define UPDATE_URL      "http://meatloaf.cc/fw/?p=meatloaf&d={{DEVICE_ID}}&a="
//#define UPDATE_URL "http://meatloaf.cc/fw/meatloaf.4MB.bin"
//#define UPDATE_URL      "http://meatloaf.cc/fw/meatloaf.16MB.bin"

#define SYSTEM_DIR "/.sys/"

#define HOSTNAME "meatloaf"
#define SERVER_PORT 80   // HTTPd & WebDAV Server Port
#define LISTEN_PORT 6400 // Listen to this if not connected. Set to zero to disable.

//#define DEVICE_MASK 0b01111111111111111111111111110000 //  Devices 4-30 are enabled by default
#define DEVICE_MASK   0b00000000000000000000111100000000 //  Devices 8-11
//#define DEVICE_MASK   0b00000000000000000000111000000000 //  Devices 9-11
//#define DEVICE_MASK   0b00000000000000000000000100000000 //  Device 8 only
//#define DEVICE_MASK   0b00000000000000000000001000000000 //  Device 9 only


/*
 * DEBUG SETTINGS
 */

// Enable this for verbose logging of IEC interface
#define DEBUG
#define BACKSPACE "\x08"


// Enable this for a timing test pattern on ATN, CLK, DATA, SRQ pins
//#define DEBUG_TIMING

// Enable this to show the data stream while loading
// Make sure device baud rate and monitor_speed = 921600
#define DATA_STREAM

// Enable this to show the data stream for other devices
// Listens to all commands and data to all devices
//#define IEC_SNIFFER

// Select the FileSystem in PLATFORMIO.INI file
//#define USE_SPIFFS
//#define USE_LITTLEFS
//#define USE_SDFS

// Enable WEB SERVER or WEBDAV
//#define ML_WEB_SERVER
#define ML_WEBDAV
#define ML_MDNS

// // Format storage if a valid file system is not found
// #define AUTO_FORMAT true
// #define FORMAT_LITTLEFS_IF_FAILED true

// #if defined USE_SPIFFS
// #define FS_TYPE "SPIFFS"
// #elif defined USE_LITTLEFS
// #define FS_TYPE "LITTLEFS"
// #elif defined USE_SDFS
// #define FS_TYPE "SDFS"
// #endif



#endif // GLOBAL_DEFINES_H
