#ifndef ML_STUB_TASK_H
#define ML_STUB_TASK_H
#include "FreeRTOS.h"
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
static inline void vTaskDelay(TickType_t t) { (void)t; }
static inline void vTaskDelete(TaskHandle_t h) { (void)h; }
static inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char* n, uint32_t s, void* p, uint32_t pr, TaskHandle_t* h, BaseType_t c)
{ (void)f;(void)n;(void)s;(void)p;(void)pr;(void)c; if (h) *h = (TaskHandle_t)0; return pdPASS; }
static inline uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t h) { (void)h; return 0; }
#endif
