#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include <cstdint>
#include <cstddef>
#include <vector>

#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <esp_attr.h>
#include <esp_cpu.h>

#include "../../include/cbm_defines.h"

static DRAM_ATTR esp_cpu_cycle_count_t timer_start_cycles, timer_cycles_per_us;
#define timer_init()         timer_cycles_per_us = esp_rom_get_cpu_ticks_per_us()
#define timer_reset()        timer_start_cycles = esp_cpu_get_cycle_count()
#define timer_start()        timer_start_cycles = esp_cpu_get_cycle_count()
#define timer_stop()         while(0)
#define timer_wait_until(us) timer_wait_until_(us+0.5)
FORCE_INLINE_ATTR void timer_wait_until_(uint32_t us)
{
  esp_cpu_cycle_count_t to = us * timer_cycles_per_us;
  while( (esp_cpu_get_cycle_count()-timer_start_cycles) < to );
}

#define IEC_RELEASE(pin) ({                     \
      uint32_t _pin = pin;                      \
      uint32_t _mask = 1 << (_pin % 32);        \
      if (_pin >= 32)                           \
        GPIO.enable1_w1tc.val = _mask;          \
      else                                      \
        GPIO.enable_w1tc = _mask;               \
    })
#define IEC_ASSERT(pin) ({                      \
      uint32_t _pin = pin;                      \
      uint32_t _mask = 1 << (_pin % 32);        \
      if (_pin >= 32)                           \
        GPIO.enable1_w1ts.val = _mask;          \
      else                                      \
        GPIO.enable_w1ts = _mask;               \
    })

#ifndef IEC_INVERTED_LINES
#define IEC_IS_ASSERTED(pin) ({                                         \
      uint32_t _pin = pin;                                              \
      !((_pin >= 32 ? GPIO.in1.val : GPIO.in) & (1 << (_pin % 32)));    \
    })
#else
#define IEC_IS_ASSERTED(pin) ({                                         \
      uint32_t _pin = pin;                                              \
      !!(_pin >= 32 ? GPIO.in1.val : GPIO.in) & (1 << (_pin % 32));     \
    })
#endif /* !IEC_INVERTED_LINES */

#define IEC_SET_STATE(x) ({IEC._state = x;})

typedef enum
{
    PROTOCOL_COMMAND = 0,
    PROTOCOL_LISTEN = 1,
    PROTOCOL_TALK = 2,
} protocol_mode_t;

namespace Protocol
{
    /**
     * @brief The IEC bus protocol base class
     */
    class IECProtocol
    {
    private:
      uint64_t _transferEnded = 0;

    public:

        protocol_mode_t mode = PROTOCOL_LISTEN;

        // Fast Loader Pair Timing
        std::vector<std::vector<uint8_t>> bit_pair_timing = {
            {0, 0, 0, 0},    // Receive
            {0, 0, 0, 0}     // Send
        };

        /**
         * @brief receive byte from bus
         * @return The byte received from bus.
        */
        virtual uint8_t receiveByte() = 0;

        /**
         * @brief send byte to bus
         * @param b Byte to send
         * @param eoi Signal EOI (end of Information)
         * @return true if send was successful.
        */
        virtual bool sendByte(uint8_t b, bool signalEOI) = 0;

        int waitForSignals(int pin1, int state1, int pin2, int state2, int timeout);
        void transferDelaySinceLast(size_t minimumDelay);
    };
};

#define transferEnd() transferDelaySinceLast(0)

#endif /* IECPROTOCOLBASE_H */
