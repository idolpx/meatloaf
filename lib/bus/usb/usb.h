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

// USB composite device: CDC ACM (serial console) + MSC (mass storage)
//
// LUN 0 — internal flash (LittleFS partition, read-only from host)
// LUN 1 — SD card (FAT, read-write; host sees it as a removable drive)
//
// References:
//   https://github.com/espressif/esp-usb/tree/master
//   https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/device

#pragma once

#ifdef CONFIG_IDF_TARGET_ESP32S3

#include <stdint.h>
#include <stdbool.h>
#include "sdmmc_cmd.h"
#include "esp_partition.h"

// Logical unit numbers exposed via MSC
#define USB_MSC_LUN_FLASH   0u
#define USB_MSC_LUN_SD      1u
#define USB_MSC_NUM_LUNS    2u

#define USB_MSC_SECTOR_SIZE 512u

class USBManager
{
public:
    // Call once, after filesystem init, before console init.
    void setup();

    // True while a USB host has the device mounted.
    bool is_host_active() const { return _host_active; }
    void set_host_active(bool v) { _host_active = v; }

    // Accessors used by MSC callbacks defined in usb.cpp.
    const esp_partition_t *flash_part() const { return _flash_part; }
    sdmmc_card_t          *sd_card()    const { return _sd_card; }
    void                   set_sd_card(sdmmc_card_t *card) { _sd_card = card; }

private:
    const esp_partition_t *_flash_part  = nullptr;
    sdmmc_card_t          *_sd_card     = nullptr;
    bool                   _host_active = false;
};

extern USBManager USB;

#endif // CONFIG_IDF_TARGET_ESP32S3
