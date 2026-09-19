// Host stub for ESP-IDF's error type.
//
// Pulled in by lib/console/console_baud.h, whose declarations the modem AT
// suite compiles against so the test's fake implementation cannot drift from
// the real contract. Only the type and the two codes that header's callers
// compare against are needed.
#ifndef ML_STUB_ESP_ERR_H
#define ML_STUB_ESP_ERR_H

typedef int esp_err_t;

#define ESP_OK              0
#define ESP_FAIL            (-1)
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_SUPPORTED 0x106

#endif
