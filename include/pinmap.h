
#include <soc/gpio_num.h>

/* Meatloaf Hardware Pin Mapping */
#ifndef PINMAP_H
#define PINMAP_H

// ESP32 WROVER-E
#include "pinmap/lolin-d32-pro.h"
#include "pinmap/iec-nugget.h"
#include "pinmap/fujiloaf-rev0.h"
#include "pinmap/fujinet-v1.6.h"
#include "pinmap/fujiapple-rev0.h"

// ESP32 WROOM-32
#include "pinmap/esp-wroom32.h"
#include "pinmap/esp-wroom32-pi1541.h"
#include "pinmap/ttgo-t1.h"
#include "pinmap/petdiskmaxv2.h"
#include "pinmap/wic64.h"

// ESP32 S2
#include "pinmap/lolin-s2-mini.h"

// ESP32 S3
#include "pinmap/esp32-s3-devkitc-1.h"
#include "pinmap/lolin-s3-pro.h"
#include "pinmap/lilygo-t-display-s3.h"
#include "pinmap/freenove-esp32-s3-wroom.h"
#include "pinmap/esp32-1732s019.h"
#include "pinmap/nodemcu-esp-s3-12k-kit.h"
#include "pinmap/esp32-s3-zero.h"
#include "pinmap/esp32-s3-super-mini.h"
#include "pinmap/adafruit_feather_esp32s3_tft.h"

// ESP32 C3
#include "pinmap/esp32-c3-super-mini.h"

#ifndef PIN_MODEM_ENABLE
#define PIN_MODEM_ENABLE        GPIO_NUM_NC  // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_NC  // High = UP9600 enabled
#endif

/* LED Strip */
#ifndef PIN_LED_RGB
#define PIN_LED_RGB             GPIO_NUM_NC // No RGB LED
#endif
#define RGB_LED_DATA_PIN        PIN_LED_RGB
#define RGB_LED_BRIGHTNESS      15 // max mA the LED can use determines brightness
#define RGB_LED_COUNT           5
#define RGB_LED_TYPE            WS2812B
#define RGB_LED_ORDER           GRB
#ifndef PIN_LED_RGB_PWR
#define PIN_LED_RGB_PWR         GPIO_NUM_NC
#endif

/* PS/2 Keyboard Output */
#ifndef PIN_KB_CLK
#define PIN_KB_CLK              GPIO_NUM_NC;
#define PIN_KB_DATA             GPIO_NUM_NC;
#endif

#ifdef ENABLE_ZIMODEM
#define DEFAULT_PIN_DCD GPIO_NUM_14
#define DEFAULT_PIN_CTS GPIO_NUM_13
#define DEFAULT_PIN_RTS GPIO_NUM_15 // unused
#define DEFAULT_PIN_RI  GPIO_NUM_32
#define DEFAULT_PIN_DSR GPIO_NUM_12
#define DEFAULT_PIN_DTR GPIO_NUM_27
#endif


#ifndef PIN_DEBUG
#define PIN_DEBUG		PIN_IEC_SRQ
#endif

#endif // PINMAP_H