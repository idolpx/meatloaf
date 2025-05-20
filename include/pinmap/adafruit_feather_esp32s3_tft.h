
#ifndef PINMAP_ADAFRUIT_FEATHER_ESP32S3_TFT_H
#define PINMAP_ADAFRUIT_FEATHER_ESP32S3_TFT_H

// https://www.adafruit.com/product/5483
// https://learn.adafruit.com/adafruit-esp32-s3-tft-feather
// https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf

#ifdef PINMAP_ADAFRUIT_FEATHER_ESP32S3_TFT

// ESP32-S3FH4R2
#define FLASH_SIZE              4
#define PSRAM_SIZE              2

/* SD Card */
#define PIN_SD_HOST_CS          GPIO_NUM_NC
#define PIN_SD_HOST_MISO        GPIO_NUM_NC
#define PIN_SD_HOST_MOSI        GPIO_NUM_NC
#define PIN_SD_HOST_SCK         GPIO_NUM_NC
#define PIN_CARD_DETECT         GPIO_NUM_NC

/* UART */
#define PIN_UART0_RX            GPIO_NUM_44  // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_43
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_NC
#define PIN_UART2_TX            GPIO_NUM_NC

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_0  // Capacitive Touch // keys.cpp
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_NC  // led.cpp
#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_BUS             GPIO_NUM_13
#define PIN_LED_RGB             GPIO_NUM_33
#define PIN_LED_RGB_PWR         GPIO_NUM_34

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_NC  // samlib.h
#define PIN_I2S                 GPIO_NUM_NC

/* PS/2 Keyboard Output */
#define PIN_KB_CLK              GPIO_NUM_NC
#define PIN_KB_DATA             GPIO_NUM_NC

/* I2C GPIO Expander */
#define PIN_GPIOX_SDA           GPIO_NUM_NC
#define PIN_GPIOX_SCL           GPIO_NUM_NC
#define PIN_GPIOX_INT           GPIO_NUM_NC
//#define GPIOX_ADDRESS           0x20  // PCF8575
#define GPIOX_ADDRESS           0x24  // PCA9673
//#define GPIOX_SPEED             400   // PCF8575 - 400Khz
#define GPIOX_SPEED             1000  // PCA9673 - 1000Khz / 1Mhz

/* Commodore IEC Pins */
// CLK & DATA lines in/out are split between two pins
//#define IEC_SPLIT_LINES

// Line values are inverted (7406 Hex Inverter Buffer)
//#define IEC_INVERTED_LINES

#define IEC_HAS_RESET // Reset line is available
                                                //        WIRING
                                                //      C64    DIN6
#define PIN_IEC_ATN             GPIO_NUM_18     //      ATN    3
#define PIN_IEC_CLK_IN          GPIO_NUM_17     //      CLK    4
#define PIN_IEC_CLK_OUT         GPIO_NUM_17     //    
#define PIN_IEC_DATA_IN         GPIO_NUM_16     //      DATA   5
#define PIN_IEC_DATA_OUT        GPIO_NUM_16     //    
#define PIN_IEC_SRQ             GPIO_NUM_15     //      SRQ    1
#define PIN_IEC_RESET           GPIO_NUM_14     //      RESET  6
                                                //      GND    2

/* Modem/Parallel Switch */
#define PIN_MODEM_ENABLE        GPIO_NUM_NC  // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_NC  // High = UP9600 enabled

#endif // PINMAP_ADAFRUIT_FEATHER_ESP32S3_TFT
#endif // PINMAP_ADAFRUIT_FEATHER_ESP32S3_TFT_H