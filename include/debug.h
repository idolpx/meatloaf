#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <string>

#include "ansi_codes.h"

// __PLATFORMIO_BUILD_DEBUG__ is set when build_type is set to debug in platformio.ini
// __PC_BUILD_DEBUG__ is set when build_type is set Debug in in fujinet_pc.cmake
// DBUG2 is set to enable monitor messages for a release build
//       in platformio.ini for ESP build, in fujinet_pc.cmake for PC build
#if defined(__PLATFORMIO_BUILD_DEBUG__) || defined(__PC_BUILD_DEBUG__) || defined(DBUG2)
#define DEBUG
#endif

#ifdef UNIT_TESTS
#undef DEBUG
#endif

/*
  Debugging Macros
*/
#ifdef ESP_PLATFORM
#ifdef ENABLE_CONSOLE
    #include "../lib/console/ESP32Console.h"
    #define Serial console
#else
    // Use FujiNet debug serial output
    #include "../lib/hardware/fnUART.h"
    #define Serial fnUartDebug
#endif
#endif

#ifdef DEBUG
#ifdef ESP_PLATFORM
    #define Debug_print(...) Serial.print( __VA_ARGS__ )
    #define Debug_printf(...) Serial.printf( __VA_ARGS__ )
    #define Debug_println(...) Serial.println( __VA_ARGS__ )

    #define Debug_printv(format, ...) \
                Serial.printf(ANSI_YELLOW "[%s:%u] %s(): " ANSI_GREEN_BOLD format ANSI_RESET "\r\n", \
                  __FILE__, __LINE__, __FUNCTION__, \
                  ##__VA_ARGS__)

    #define Debug_memory() {Debug_printv("Heap[%lu] Low[%lu] Task[%u]", esp_get_free_heap_size(), esp_get_free_internal_heap_size(), uxTaskGetStackHighWaterMark(NULL));}
    #define HEAP_CHECK(x) Debug_printf("HEAP CHECK %s " x "\r\n", heap_caps_check_integrity_all(true) ? "PASSED":"FAILED")
#else
    // Use util_debug_printf() helper function
    #include <utils.h>

    #define Debug_print(...) util_debug_printf(nullptr, __VA_ARGS__)
    #define Debug_printf(...) util_debug_printf(__VA_ARGS__)
    #define Debug_println(...) util_debug_printf("%s\r\n", __VA_ARGS__)
    #define Debug_printv(format, ...) {util_debug_printf( ANSI_YELLOW "[%s:%u] %s(): " ANSI_GREEN_BOLD format ANSI_RESET "\r\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);}

    #define Debug_memory()
    #define HEAP_CHECK(x) Debug_printf("HEAP CHECK %s " x "\r\n", heap_caps_check_integrity_all(true) ? "PASSED":"FAILED")
#endif
#else
    #define Debug_print(...)
    #define Debug_printf(...)
    #define Debug_println(...)
    #define Debug_printv(format, ...)

    #define Debug_memory()
    #define HEAP_CHECK(x)
#endif // !DEBUG

#endif // _DEBUG_H_
