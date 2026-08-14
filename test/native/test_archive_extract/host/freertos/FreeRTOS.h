#ifndef ML_STUB_FREERTOS_H
#define ML_STUB_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#endif
