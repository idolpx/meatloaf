// Host stub for ESP-IDF's monotonic microsecond clock.
//
// modem.cpp's now_ms() is the only consumer here, and it is used for the +++
// guard-time windows. A steady_clock reading keeps those comparisons
// meaningful on the host without pulling in anything ESP-specific.
#ifndef ML_STUB_ESP_TIMER_H
#define ML_STUB_ESP_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
#include <chrono>

static inline int64_t esp_timer_get_time(void)
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}
#endif

#endif
