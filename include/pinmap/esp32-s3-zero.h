#ifndef PINMAP_ESP32_S3_ZERO_H
#define PINMAP_ESP32_S3_ZERO_H

// https://www.waveshare.com/esp32-s3-zero.htm
// https://www.waveshare.com/wiki/ESP32-S3-Zero
// https://files.waveshare.com/wiki/ESP32-S3-Zero/ESP32-S3-Zero-Sch.pdf
// https://www.espressif.com.cn/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
// https://randomnerdtutorials.com/esp32-s3-devkitc-pinout-guide/

#ifdef PINMAP_ESP32_S3_ZERO

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

/* LED */
#define PIN_LED_WIFI            GPIO_NUM_NC
#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_RGB             GPIO_NUM_21

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_NC  // samlib.h
#define PIN_I2S                 GPIO_NUM_42

/* I2C GPIO Expander */
#define PIN_GPIOX_SDA           GPIO_NUM_9
#define PIN_GPIOX_SCL           GPIO_NUM_10
#define PIN_GPIOX_INT           GPIO_NUM_41
//#define GPIOX_ADDRESS           0x20  // PCF8575
#define GPIOX_ADDRESS           0x24  // PCA9673
//#define GPIOX_SPEED             400   // PCF8575 - 400Khz
#define GPIOX_SPEED             1000  // PCA9673 - 1000Khz / 1Mhz

/* Commodore IEC Pins */
#define IEC_HAS_RESET // Reset line is available
                                                //       WIRING
                                                //      C64    DIN6
#define PIN_IEC_ATN             GPIO_NUM_4      //      ATN    3
#define PIN_IEC_CLK_IN          GPIO_NUM_5      //      CLK    4
#define PIN_IEC_CLK_OUT         GPIO_NUM_5      //    
#define PIN_IEC_DATA_IN         GPIO_NUM_6      //      DATA   5
#define PIN_IEC_DATA_OUT        GPIO_NUM_6      //    
#define PIN_IEC_SRQ             GPIO_NUM_7      //      SRQ    1
#define PIN_IEC_RESET           GPIO_NUM_8      //      RESET  6
                                                //      GND    2

/* Modem/Parallel Switch */
#define PIN_MODEM_ENABLE        GPIO_NUM_2   // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_15  // High = UP9600 enabled

#endif // PINMAP_ESP32_S3_ZERO
#endif // PINMAP_ESP32_S3_ZERO_H