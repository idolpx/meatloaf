
#include <soc/gpio_num.h>

#ifndef PINMAP_DEFAULTS_H
#define PINMAP_DEFAULTS_H

// Defaults
#define LEDS_INVERTED           0

// Commodore User Port Serial/Parallel Mode
#define PIN_MODEM_ENABLE        GPIO_NUM_NC  // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_NC  // High = UP9600 enabled

// LED Strip
#define PIN_LED_RGB             GPIO_NUM_NC // No RGB LED
#define RGB_LED_BRIGHTNESS      15 // max mA the LED can use determines brightness
#define RGB_LED_COUNT           5
#define RGB_LED_TYPE            WS2812B
#define RGB_LED_ORDER           GRB
#define PIN_LED_RGB_PWR         GPIO_NUM_NC

// PS/2 Keyboard
#define PIN_KB_CLK              GPIO_NUM_NC;
#define PIN_KB_DATA             GPIO_NUM_NC;

// Zimodem
#define DEFAULT_PIN_DCD GPIO_NUM_14
#define DEFAULT_PIN_CTS GPIO_NUM_13
#define DEFAULT_PIN_RTS GPIO_NUM_15 // unused
#define DEFAULT_PIN_RI  GPIO_NUM_32
#define DEFAULT_PIN_DSR GPIO_NUM_12
#define DEFAULT_PIN_DTR GPIO_NUM_27

#endif // PINMAP_DEFAULTS_H