// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
//
// USB composite device: CDC ACM (serial console) + MSC (mass storage)
//
// The CDC interface replaces the USB Serial/JTAG debug interface.
// ESP-IDF routes stdin/stdout through it automatically when
// CONFIG_ESP_CONSOLE_USB_CDC=y — no extra wiring needed here.
//
// MSC exposes two logical units:
//   LUN 0 — internal flash  (LittleFS, read-only from host)
//   LUN 1 — SD card          (FAT, read-write)
//
// The SD card is accessed at the raw-sector level via sdmmc_read/write_sectors,
// bypassing the VFS/FATFS layer. The host therefore sees the native FAT volume.
// Note: while a host has the SD LUN mounted, the firmware's own VFS access to
// /sd will race with host writes. Set USB.is_host_active() checks in any
// firmware code that needs to avoid concurrent SD access.
//
// Flash writes from the host are rejected (read-only flag). Writing raw blocks
// to a live LittleFS partition from the host would corrupt the filesystem.

#ifdef CONFIG_IDF_TARGET_ESP32S3

#include "usb.h"

#include "fnFsSD.h"            // fnSDFAT, fnSDFAT.card()
#include "fsFlash.h"           // fsFlash

#include "tinyusb.h"           // tinyusb_driver_install(), tinyusb_config_t
#include "class/msc/msc.h"     // SCSI command codes, sense keys
#include "class/msc/msc_device.h" // tud_msc_* weak declarations

#include "esp_partition.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "usb";

// Global singleton — referenced from MSC callbacks below.
USBManager USB;

// ─── USBManager::setup ────────────────────────────────────────────────────────

void USBManager::setup()
{
    // Locate the LittleFS data partition (ESP-IDF uses SUBTYPE_DATA_SPIFFS
    // for LittleFS partitions as both share the same subtype value).
    _flash_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    if (!_flash_part) {
        ESP_LOGW(TAG, "LittleFS partition not found — flash LUN unavailable");
    }

    // Grab the sdmmc_card_t* that fnSDFAT holds after its own start().
    // If SD hasn't started yet (card absent) this is nullptr; the MSC
    // test-unit-ready callback returns false in that case.
    _sd_card = fnSDFAT.card();

    // Install TinyUSB driver.  Descriptor content (VID/PID, strings, etc.)
    // comes from CONFIG_TINYUSB_DESC_* Kconfig entries; passing all-zeros
    // here selects those defaults.
    const tinyusb_config_t tusb_cfg = {};
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    ESP_LOGI(TAG, "USB composite CDC+MSC initialised");
}

// ─── TinyUSB device-event callbacks ──────────────────────────────────────────

extern "C" {

void tud_mount_cb(void)
{
    USB.set_host_active(true);
    // Lazy-refresh SD pointer: USB.setup() runs before fnSDFAT.start() in
    // main_setup(), so _sd_card may be nullptr at install time.
    if (!USB.sd_card() && fnSDFAT.running())
        USB.set_sd_card(fnSDFAT.card());
    ESP_LOGI(TAG, "USB host mounted");
}

void tud_umount_cb(void)
{
    USB.set_host_active(false);
    ESP_LOGI(TAG, "USB host unmounted");
}

void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; }
void tud_resume_cb(void) {}

// ─── MSC callbacks ────────────────────────────────────────────────────────────

uint8_t tud_msc_get_maxlun_cb(void)
{
    return USB_MSC_NUM_LUNS;
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if (lun == USB_MSC_LUN_FLASH) return USB.flash_part() != nullptr;
    if (lun == USB_MSC_LUN_SD)    return USB.sd_card()    != nullptr;
    return false;
}

void tud_msc_inquiry_cb(uint8_t lun,
                         uint8_t vendor_id[8],
                         uint8_t product_id[16],
                         uint8_t product_rev[4])
{
    memcpy(vendor_id,   "MLOAF   ", 8);
    memcpy(product_rev, "1.00",     4);
    if (lun == USB_MSC_LUN_FLASH)
        memcpy(product_id, "Flash (LittleFS)", 16);   // exactly 16 chars
    else
        memcpy(product_id, "SD Card         ", 16);   // 7 + 9 spaces = 16
}

bool tud_msc_capacity_cb(uint8_t lun,
                          uint32_t *block_count,
                          uint16_t *block_size)
{
    if (lun == USB_MSC_LUN_FLASH && USB.flash_part()) {
        *block_size  = USB_MSC_SECTOR_SIZE;
        *block_count = USB.flash_part()->size / USB_MSC_SECTOR_SIZE;
        return true;
    }
    if (lun == USB_MSC_LUN_SD && USB.sd_card()) {
        *block_size  = (uint16_t)USB.sd_card()->csd.sector_size;
        *block_count = (uint32_t)USB.sd_card()->csd.capacity;
        return true;
    }
    return false;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition,
                            bool start, bool load_eject)
{
    (void)lun; (void)power_condition; (void)start; (void)load_eject;
    return true;
}

// Flash is read-only from the host to protect the live LittleFS metadata.
bool tud_msc_is_writable_cb(uint8_t lun)
{
    return lun == USB_MSC_LUN_SD;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba,
                           uint32_t offset, void *buffer, uint32_t bufsize)
{
    if (lun == USB_MSC_LUN_FLASH && USB.flash_part()) {
        uint32_t addr = lba * USB_MSC_SECTOR_SIZE + offset;
        if (esp_partition_read(USB.flash_part(), addr, buffer, bufsize) == ESP_OK)
            return (int32_t)bufsize;
        return -1;
    }
    if (lun == USB_MSC_LUN_SD && USB.sd_card()) {
        // sdmmc_read_sectors operates in whole card-sectors; offset must be 0
        // for block-aligned MSC transfers (TinyUSB guarantees this).
        if (offset != 0) return -1;
        uint32_t sector_sz = USB.sd_card()->csd.sector_size;
        uint32_t count     = bufsize / sector_sz;
        if (sdmmc_read_sectors(USB.sd_card(), buffer, lba, count) == ESP_OK)
            return (int32_t)bufsize;
        return -1;
    }
    return -1;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba,
                            uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    // Flash writes rejected (tud_msc_is_writable_cb already returns false for
    // LUN_FLASH, but guard here as well in case the host ignores write-protect).
    if (lun == USB_MSC_LUN_SD && USB.sd_card()) {
        if (offset != 0) return -1;
        uint32_t sector_sz = USB.sd_card()->csd.sector_size;
        uint32_t count     = bufsize / sector_sz;
        if (sdmmc_write_sectors(USB.sd_card(), buffer, lba, count) == ESP_OK)
            return (int32_t)bufsize;
        return -1;
    }
    return -1;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                         void *buffer, uint16_t bufsize)
{
    (void)buffer; (void)bufsize;

    switch (scsi_cmd[0]) {
        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
            // Allow the host to lock/unlock media — we just acknowledge.
            return 0;

        default:
            tud_msc_set_sense(lun,
                              SCSI_SENSE_ILLEGAL_REQUEST,
                              0x20, // ASC: Invalid Command Operation Code
                              0x00);
            return -1;
    }
}

} // extern "C"

#endif // CONFIG_IDF_TARGET_ESP32S3
