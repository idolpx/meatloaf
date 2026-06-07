#pragma once
#ifdef CONFIG_IDF_TARGET_ESP32S3

#include <stdbool.h>
#include "sdmmc_cmd.h"

class USBManager {
public:
    // Call before console.begin() — installs TinyUSB driver (CDC + MSC skeleton).
    void setup();
    // Call after fnSDFAT.start() — registers SD card as MSC LUN 0.
    void setup_storage();

    bool          is_host_active() const { return _host_active; }
    void          set_host_active(bool v) { _host_active = v; }
    sdmmc_card_t *sd_card()        const { return _sd_card; }

private:
    sdmmc_card_t *_sd_card     = nullptr;
    void         *_sd_handle   = nullptr;  // tinyusb_msc_storage_handle_t
    bool          _host_active = false;
};

extern USBManager USB;
#endif // CONFIG_IDF_TARGET_ESP32S3
