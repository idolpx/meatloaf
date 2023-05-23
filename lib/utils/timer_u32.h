//
// https://github.com/OliviliK/ESP32_timer_u32
//

#include "soc/frc_timer_reg.h"

#if defined(CONFIG_ESP_TIMER_IMPL_FRC2)
#define timer_u32() (REG_READ(0x3ff47024))      // FRC2
#define _TICKS_PER_US (80)

#elif defined(CONFIG_ESP_TIMER_IMPL_TG0_LAC)
#include "esp_timer.h"
#define timer_u32() (esp_timer_get_time())      // TG0_LAC
#define _TICKS_PER_US (1)

#else
                                                // SYSTIMER
__attribute__((always_inline)) static inline uint32_t timer_u32(void) {
   REG_WRITE(0x3f423038,1UL << 31);             // Write SYSTIMER_TIMER_UPDATE bit 
   while (!REG_GET_BIT(0x3f423038,1UL << 30));  // Check SYSTIMER_TIMER_VALUE_VALID
   return REG_READ(0x3f423040);                 // Read SYSTIMER_VALUE_LO_REG
}
#define _TICKS_PER_US (80)
#endif

__attribute__((always_inline)) static inline float timer_delta_ns(uint32_t tics) {
   return tics * (1000.0 / _TICKS_PER_US);
}

__attribute__((always_inline)) static inline float timer_delta_us(uint32_t tics) {
   return tics * (1.0 / _TICKS_PER_US);
}

__attribute__((always_inline)) static inline float timer_delta_ms(uint32_t tics) {
   return tics * (0.001 / _TICKS_PER_US);
}

__attribute__((always_inline)) static inline float timer_delta_s(uint32_t tics) {
   return tics * (0.000001 / _TICKS_PER_US);
}