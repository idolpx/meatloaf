
#ifndef PINMAP_POCKET_DONGLE_S3_H
#define PINMAP_POCKET_DONGLE_S3_H

// https://github.com/ronenkr/Pocket-Dongle-S3
// https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf

#ifdef PINMAP_POCKET_DONGLE_S3

// ESP32-S3R8
#define FLASH_SIZE              16
#define PSRAM_SIZE              8

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_NC // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC // fnSystem.h

#define PIN_SD_HOST_CS          GPIO_NUM_47
#define PIN_SD_HOST_MISO        GPIO_NUM_16
#define PIN_SD_HOST_MOSI        GPIO_NUM_18
#define PIN_SD_HOST_SCK         GPIO_NUM_17


/* UART */
#define PIN_UART0_RX            GPIO_NUM_44  // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_43
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_NC
#define PIN_UART2_TX            GPIO_NUM_NC

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_0  // keys.cpp
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_1   // led.cpp
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_RGB             GPIO_NUM_2

/* LCD */
// .96in ST7735 - 160x80
#define TFT_WIDTH               160
#define TFT_HEIGHT              80
#define PIN_TFT_SCLK            GPIO_NUM_10
#define PIN_TFT_MOSI            GPIO_NUM_11
#define PIN_TFT_CS              GPIO_NUM_12
#define PIN_TFT_DC              GPIO_NUM_13
#define PIN_TFT_RST             GPIO_NUM_14
#define PIN_TFT_BL              GPIO_NUM_NC
#define TFT_OFFSETX             0
#define TFT_OFFSETY             0
//#define TFT_INVERSION_ON
//#define TFT_SPI_FREQUENCY       40000000 // 40MHz
#define TFT_SPI_FREQUENCY       60000000 // 60MHz


/* Audio Output */
#define PIN_DAC1                GPIO_NUM_NC // samlib.h
#define PIN_I2S                 GPIO_NUM_NC

/* I2C GPIO Expander */
#define PIN_GPIOX_SDA           GPIO_NUM_NC
#define PIN_GPIOX_SCL           GPIO_NUM_NC
#define PIN_GPIOX_INT           GPIO_NUM_NC
//#define GPIOX_ADDRESS           0x20  // PCF8575
//#define GPIOX_ADDRESS           0x24  // PCA9673
//#define GPIOX_SPEED             400   // PCF8575 - 400Khz
//#define GPIOX_SPEED             1000  // PCA9673 - 1000Khz / 1Mhz


/* Commodore IEC Pins */
// CLK & DATA lines in/out are split between two pins
//#define IEC_SPLIT_LINES

// Line values are inverted (7406 Hex Inverter Buffer)
//#define IEC_INVERTED_LINES

// Reset line is available
#define IEC_HAS_RESET
                                                //    WIRING
                                                //  C64    DIN6
#define PIN_IEC_ATN             GPIO_NUM_4      //  ATN    3
#define PIN_IEC_CLK_IN          GPIO_NUM_5      //  CLK    4
#define PIN_IEC_CLK_OUT         GPIO_NUM_5      //
#define PIN_IEC_DATA_IN         GPIO_NUM_6      //  DATA   5
#define PIN_IEC_DATA_OUT        GPIO_NUM_6      //
#define PIN_IEC_SRQ             GPIO_NUM_7      //  SRQ    1
#define PIN_IEC_RESET           GPIO_NUM_8      //  RESET  6
//                            SIDE OF SD SLOT   //  GND    2  


/* Modem/Parallel Switch */
#define PIN_MODEM_ENABLE        GPIO_NUM_NC  // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_NC  // High = UP9600 enabled

#endif // PINMAP_POCKET_DONGLE_S3
#endif // PINMAP_POCKET_DONGLE_S3_H