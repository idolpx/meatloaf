// Meatloaf USB composite device: CDC ACM (serial console) + MSC (mass storage)
//
// CDC: the console (linenoise REPL) is routed through USB CDC automatically
//      when CONFIG_ESP_CONSOLE_USB_CDC=y — no extra code needed here.
//
// MSC: exposes the SD card as a FAT volume. The SD card is always in
//      "USB mode" (raw-sector access), so fnSDFAT's /sd VFS mount is never
//      unmounted. Do not write to /sd from firmware while the USB host has the
//      drive mounted to avoid FAT corruption.
//
// Flash (LittleFS) is NOT exposed via MSC: the tinyusb_msc high-level API
// requires a wear-levelling handle (FAT over WL), which LittleFS doesn't use.

// esp_log.h includes sdkconfig.h at line 12, making CONFIG_IDF_TARGET_ESP32S3
// available before the guard below. ESP-IDF 5.4 does not force-include
// sdkconfig.h, so a bare #ifdef at the top of the file would silently evaluate
// to false and compile the entire translation unit away.
#include "esp_log.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3

#include "usb.h"
#include "fnFsSD.h"         // fnSDFAT, fnSDFAT.card()

#include "tinyusb.h"                // tinyusb_driver_install(), tinyusb_config_t
#include "tinyusb_default_config.h" // TINYUSB_DEFAULT_TASK_SIZE / PRIO / AFFINITY constants
#include "tinyusb_msc.h"            // tinyusb_msc_install_driver(), tinyusb_msc_new_storage_sdmmc()
#if CONFIG_TINYUSB_CDC_ENABLED
#include "tinyusb_cdc_acm.h"        // tinyusb_cdcacm_init()
#endif

#include <string.h>

static const char *TAG = "usb";

// Global singleton
USBManager USB;

// ─── Event callbacks ──────────────────────────────────────────────────────────

static void usb_event_cb(tinyusb_event_t *event, void *arg)
{
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        USB.set_host_active(true);
        ESP_LOGI(TAG, "USB host attached");
        break;
    case TINYUSB_EVENT_DETACHED:
        USB.set_host_active(false);
        ESP_LOGI(TAG, "USB host detached");
        break;
    default:
        break;
    }
}

static void msc_event_cb(tinyusb_msc_storage_handle_t handle,
                         tinyusb_msc_event_t *event, void *arg)
{
    if (event->id == TINYUSB_MSC_EVENT_MOUNT_FAILED)
        ESP_LOGW(TAG, "MSC storage mount event failed (id=%d mp=%d)",
                 event->id, event->mount_point);
}

// ─── USBManager::setup ────────────────────────────────────────────────────────

void USBManager::setup()
{
    // Install MSC driver before TinyUSB so the LUN table exists when the
    // host enumerates the device after tinyusb_driver_install().
    //
    // auto_mount_off=1: prevents the component from calling esp_vfs_fat
    // mount/unmount on USB connect/disconnect. fnSDFAT owns the /sd VFS
    // mount; we keep the SD card permanently in "USB mode" so raw-sector
    // access is always available to the host without disturbing fnSDFAT.
    tinyusb_msc_driver_config_t msc_drv_cfg = {};
    msc_drv_cfg.user_flags.auto_mount_off = 1;
    msc_drv_cfg.callback     = msc_event_cb;
    msc_drv_cfg.callback_arg = nullptr;
    esp_err_t msc_err = tinyusb_msc_install_driver(&msc_drv_cfg);
    if (msc_err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_msc_install_driver failed: %s", esp_err_to_name(msc_err));
        return;
    }

    // CDC ACM must be initialized before tinyusb_driver_install() so the
    // interface descriptor is present when the host enumerates the device.
#if CONFIG_TINYUSB_CDC_ENABLED
    const tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port                    = TINYUSB_CDC_ACM_0,
        .callback_rx                 = nullptr,
        .callback_rx_wanted_char     = nullptr,
        .callback_line_state_changed = nullptr,
        .callback_line_coding_changed = nullptr,
    };
    esp_err_t cdc_err = tinyusb_cdcacm_init(&acm_cfg);
    if (cdc_err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_cdcacm_init failed: %s", esp_err_to_name(cdc_err));
        return;
    }
#endif

    // Install TinyUSB device driver. task.size and task.priority must be
    // non-zero — the component rejects 0 values with ESP_ERR_INVALID_ARG.
    // Use the component's own default constants; other fields zero-init to
    // their natural defaults (port=FS0, no VBUS monitoring, NULL descriptors
    // — the stack falls back to Kconfig-supplied descriptor content).
    tinyusb_config_t tusb_cfg = {};
    tusb_cfg.task.size     = TINYUSB_DEFAULT_TASK_SIZE;      // 4096 B
    tusb_cfg.task.priority = TINYUSB_DEFAULT_TASK_PRIO;      // 5
    tusb_cfg.task.xCoreID  = TINYUSB_DEFAULT_TASK_AFFINITY;  // CPU1 on dual-core
    tusb_cfg.event_cb      = usb_event_cb;
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    ESP_LOGI(TAG, "USB composite CDC+MSC driver installed");
}

// ─── USBManager::setup_storage ────────────────────────────────────────────────

void USBManager::setup_storage()
{
    _sd_card = fnSDFAT.card();
    if (!_sd_card) {
        ESP_LOGW(TAG, "SD card not available — MSC LUN not registered");
        return;
    }

    // Register the SD card as LUN 0 for USB raw-sector access.
    //
    // TINYUSB_MSC_STORAGE_MOUNT_USB: the component treats the card as
    // permanently owned by the USB host (raw-sector I/O via sdmmc_read/write
    // _sectors). It never calls esp_vfs_fat_register/unregister, so fnSDFAT's
    // /sd mount is not disturbed. tud_msc_test_unit_ready_cb() returns true
    // immediately when a host connects.
    //
    // fat_fs fields are stored but never used for mounting in this mode;
    // do_not_format=true guards against an accidental format if the code path
    // is ever reached.
    tinyusb_msc_storage_config_t sd_cfg = {};
    sd_cfg.medium.card             = _sd_card;
    sd_cfg.fat_fs.do_not_format    = true;
    sd_cfg.mount_point             = TINYUSB_MSC_STORAGE_MOUNT_USB;

    esp_err_t ret = tinyusb_msc_new_storage_sdmmc(
        &sd_cfg, (tinyusb_msc_storage_handle_t *)&_sd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_msc_new_storage_sdmmc failed: %s",
                 esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "SD card registered as MSC LUN 0 (%lu sectors)",
             (unsigned long)_sd_card->csd.capacity);
}

#endif // CONFIG_IDF_TARGET_ESP32S3
