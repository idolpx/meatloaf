#ifndef PINMAP_ESP32_S3_CARRIER_CLAUSS_V1_1_H
#define PINMAP_ESP32_S3_CARRIER_CLAUSS_V1_1_H

#include <soc/gpio_num.h>

#ifdef PINMAP_ESP32_S3_CARRIER_CLAUSS_V1_1

#define PINMAP_ESP32S3
#define PSRAM_SIZE                    8

/* Hardware capabilities shared by both ClausS V1.1 memory variants. */
#define MEATLOAF_CARD_DETECT_HOTPLUG
#define IEC_SPLIT_ATN
#define IEC_SPLIT_RESET

/* SD card (SPI2). Card detect is active LOW and externally pulled up. */
#define PIN_CARD_DETECT               GPIO_NUM_3
#define PIN_CARD_DETECT_FIX           GPIO_NUM_NC
#define PIN_SD_HOST_CS                GPIO_NUM_14
#define PIN_SD_HOST_MISO              GPIO_NUM_13
#define PIN_SD_HOST_SCK               GPIO_NUM_12
#define PIN_SD_HOST_MOSI              GPIO_NUM_11

/* W5500 Ethernet (SPI3). */
#define ETHERNET_SPI_HOST             SPI3_HOST
#define PIN_ETHERNET_MISO             GPIO_NUM_40
#define PIN_ETHERNET_MOSI             GPIO_NUM_41
#define PIN_ETHERNET_SCK              GPIO_NUM_39
#define ETHERNET_SPI_CLOCK_MHZ        10
#define PIN_ETHERNET_CS               GPIO_NUM_10
#define PIN_ETHERNET_INT              GPIO_NUM_9
#define PIN_ETHERNET_RESET            GPIO_NUM_NC

/* UART remains available on the ESP32-S3 module headers. */
#define PIN_UART0_RX                  GPIO_NUM_44
#define PIN_UART0_TX                  GPIO_NUM_43
#define PIN_UART1_RX                  GPIO_NUM_NC
#define PIN_UART1_TX                  GPIO_NUM_NC
#define PIN_UART2_RX                  GPIO_NUM_NC
#define PIN_UART2_TX                  GPIO_NUM_NC

/* Buttons and indicators. The discrete status LEDs are active LOW. */
#define PIN_BUTTON_A                  GPIO_NUM_0
#define PIN_BUTTON_B                  GPIO_NUM_NC
#define PIN_BUTTON_C                  GPIO_NUM_NC
#define PIN_LED_WIFI                  GPIO_NUM_1
#define PIN_LED_BUS                   GPIO_NUM_2
#define PIN_LED_BT                    GPIO_NUM_NC
#define PIN_LED_RGB                   GPIO_NUM_48
#define LEDS_INVERTED                 1

/* No DAC, I2S, modem switch, or GPIO expander is fitted on this board. */
#define PIN_DAC1                      GPIO_NUM_NC
#define PIN_I2S                       GPIO_NUM_NC
#define PIN_MODEM_ENABLE              GPIO_NUM_NC
#define PIN_MODEM_UP9600              GPIO_NUM_NC

/* Commodore IEC: non-inverting inputs and inverted NPN open-collector outputs. */
#define IEC_HAS_RESET
#define PIN_IEC_ATN_IN                GPIO_NUM_4
#define PIN_IEC_ATN_OUT               GPIO_NUM_15
#define PIN_IEC_ATN                   PIN_IEC_ATN_IN
#define PIN_IEC_CLK_IN                GPIO_NUM_5
#define PIN_IEC_CLK_OUT               GPIO_NUM_17
#define PIN_IEC_DATA_IN               GPIO_NUM_6
#define PIN_IEC_DATA_OUT              GPIO_NUM_16
#define PIN_IEC_SRQ_IN                GPIO_NUM_7
#define PIN_IEC_SRQ_OUT               GPIO_NUM_18
#define PIN_IEC_RESET_IN              GPIO_NUM_8
#define PIN_IEC_RESET_OUT             GPIO_NUM_46
#define PIN_IEC_RESET                 PIN_IEC_RESET_IN

#ifdef __cplusplus
namespace esp32_s3_carrier_clauss_v1_1_pin_validation
{
template<int... Pins> struct unique_pins;

template<> struct unique_pins<>
{
    static constexpr bool value = true;
};

template<int Head, int... Tail> struct unique_pins<Head, Tail...>
{
    static constexpr bool value = ((Head != Tail) && ...) && unique_pins<Tail...>::value;
};

// Every physical function must have a dedicated GPIO on this carrier board.
static_assert(unique_pins<
    PIN_BUTTON_A, PIN_LED_WIFI, PIN_LED_BUS, PIN_CARD_DETECT,
    PIN_IEC_ATN_IN, PIN_IEC_ATN_OUT, PIN_IEC_CLK_IN, PIN_IEC_CLK_OUT,
    PIN_IEC_DATA_IN, PIN_IEC_DATA_OUT, PIN_IEC_SRQ_IN, PIN_IEC_SRQ_OUT,
    PIN_IEC_RESET_IN, PIN_IEC_RESET_OUT,
    PIN_SD_HOST_MOSI, PIN_SD_HOST_MISO, PIN_SD_HOST_SCK, PIN_SD_HOST_CS,
    PIN_ETHERNET_MOSI, PIN_ETHERNET_MISO, PIN_ETHERNET_SCK,
    PIN_ETHERNET_CS, PIN_ETHERNET_INT, PIN_LED_RGB
>::value, "MeatLoaf Carrier Board ClausS V1.1 has an unintended GPIO assignment conflict");

static_assert(PIN_ETHERNET_CS != PIN_SD_HOST_CS,
              "MeatLoaf Carrier Board ClausS V1.1 SD and W5500 require independent chip selects");
}
#endif

#endif // PINMAP_ESP32_S3_CARRIER_CLAUSS_V1_1
#endif // PINMAP_ESP32_S3_CARRIER_CLAUSS_V1_1_H
