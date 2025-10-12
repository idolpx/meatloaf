#ifndef PINMAP_WIC64_H
#define PINMAP_WIC64_H

// https://wic64.net/web/
// https://www.espboards.dev/esp32/esp32doit-devkit-v1/
// https://www.espressif.com.cn/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf

#ifdef PINMAP_WIC64

// ESP32-WROOM-32
#define FLASH_SIZE              4
//#define PSRAM_SIZE              8

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_NC // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC // fnSystem.h

#define PIN_SD_HOST_CS          GPIO_NUM_NC
#define PIN_SD_HOST_MISO        GPIO_NUM_NC
#define PIN_SD_HOST_MOSI        GPIO_NUM_NC
#define PIN_SD_HOST_SCK         GPIO_NUM_NC

/* UART */
#define PIN_UART0_RX            GPIO_NUM_3
#define PIN_UART0_TX            GPIO_NUM_1
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_NC
#define PIN_UART2_TX            GPIO_NUM_NC

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_0
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_2
#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_BUS             GPIO_NUM_2
#define PIN_LED_RGB             GPIO_NUM_NC
#define LEDS_INVERTED           1

/* LCD */
#define LCD_SSD1306
#define LCD_WIDTH               128
#define LCD_HEIGHT              64
#define LCD_ADDRESS             0
#define PIN_LCD_SDA             GPIO_NUM_13
#define PIN_LCD_SCL             GPIO_NUM_15

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_NC // samlib.h


/* Commodore IEC Pins */
// CLK & DATA lines in/out are split between two pins
//#define IEC_SPLIT_LINES

// Line values are inverted (7406 Hex Inverter Buffer)
//#define IEC_INVERTED_LINES

// Reset line is available
#define IEC_HAS_RESET
                                                //            WIRING
                                                //  C64    DIN6    D32Pro          TFT
#define PIN_IEC_ATN             GPIO_NUM_32     //  ATN    3       A T-LED 32      10 (PURPLE)
#define PIN_IEC_CLK_IN          GPIO_NUM_33     //  CLK    4       A T-RST 33      8  (BROWN)
#define PIN_IEC_CLK_OUT         GPIO_NUM_33     //
#define PIN_IEC_DATA_IN         GPIO_NUM_12     //  DATA   5       T-CS 14         2  (BLACK)
#define PIN_IEC_DATA_OUT        GPIO_NUM_12     //
#define PIN_IEC_SRQ             GPIO_NUM_5      //  SRQ    1       T-DC 27         7  (ORANGE)
#define PIN_IEC_RESET           GPIO_NUM_34     //  RESET  6       A 34            N/C
                                                //  GND    2       GND             9  (GREY)

                                                
#define PIN_PARALLEL_PA2        GPIO_NUM_27     // Direction: HIGH = C64 => ESP, LOW = ESP => C64
#define PIN_PARALLEL_PC2        GPIO_NUM_14     // Handshake: C64 => ESP (ack/strobe: byte read from/written to port) (rising edge)
#define PIN_PARALLEL_FLAG2      GPIO_NUM_26     // Handshake: ESP => C64 (ack/strobe: byte read from/written to port) (falling edge)

#define PIN_PARALLEL_DATA0        GPIO_NUM_16
#define PIN_PARALLEL_DATA1        GPIO_NUM_17
#define PIN_PARALLEL_DATA2        GPIO_NUM_18
#define PIN_PARALLEL_DATA3        GPIO_NUM_19
#define PIN_PARALLEL_DATA4        GPIO_NUM_21
#define PIN_PARALLEL_DATA5        GPIO_NUM_22
#define PIN_PARALLEL_DATA6        GPIO_NUM_23
#define PIN_PARALLEL_DATA7        GPIO_NUM_25

#endif // PINMAP_WIC64
#endif // PINMAP_WIC64_H