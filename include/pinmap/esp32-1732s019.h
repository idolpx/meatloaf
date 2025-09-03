
#ifndef PINMAP_ESP32_1732S019_H
#define PINMAP_ESP32_1732S019_H

// https://www.surenoo.com/products/23377371
// https://github.com/OttoMeister/ESP32-1732S019
// http://pan.jczn1688.com/directlink/1/ESP32%20module/1.9inch_ESP32-1732S019.zip
// https://www.espressif.com.cn/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf

#ifdef PINMAP_ESP32_1732S019

// ESP32-S3-WROOM-1-N16R8
#define FLASH_SIZE              16
#define PSRAM_SIZE              8

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_NC // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC // fnSystem.h

#define PIN_SD_HOST_CS          GPIO_NUM_41
#define PIN_SD_HOST_MISO        GPIO_NUM_40  // SD_DATA (MISO/DAT0/DATA OUT)
#define PIN_SD_HOST_MOSI        GPIO_NUM_38  // SD_CMD (MOSI/CMD/DATA IN)
#define PIN_SD_HOST_SCK         GPIO_NUM_39  // SD_CLK (SCK/CLK)

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
#define PIN_LED_WIFI            GPIO_NUM_2  // led.cpp
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_RGB             GPIO_NUM_48

/* LCD */
#define ST7789_DRIVER  
#define TFT_WIDTH               220
#define TFT_HEIGHT              320
#define PIN_TFT_MOSI            GPIO_NUM_13
#define PIN_TFT_SCLK            GPIO_NUM_12
#define PIN_TFT_CS              GPIO_NUM_10
#define PIN_TFT_DC              GPIO_NUM_11
#define PIN_TFT_RST             GPIO_NUM_1
#define PIN_TFT_BL              GPIO_NUM_14
#define TFT_OFFSETX             0
#define TFT_OFFSETY             0
//#define TFT_INVERSION_ON
//#define TFT_SPI_FREQUENCY       40000000 // 40MHz
#define TFT_SPI_FREQUENCY       60000000 // 60MHz

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_NC // samlib.h
#define PIN_I2S                 GPIO_NUM_42

/* I2C GPIO Expander */
#define PIN_GPIOX_SDA           GPIO_NUM_16
#define PIN_GPIOX_SCL           GPIO_NUM_17
#define PIN_GPIOX_INT           GPIO_NUM_18
#define GPIOX_ADDRESS           0x20  // PCF8575
//#define GPIOX_ADDRESS           0x24  // PCA9673
#define GPIOX_SPEED             400   // PCF8575 - 400Khz
//#define GPIOX_SPEED             1000  // PCA9673 - 1000Khz / 1Mhz

/* Commodore IEC Pins */
// CLK & DATA lines in/out are split between two pins
//#define IEC_SPLIT_LINES

// Line values are inverted (7406 Hex Inverter Buffer)
//#define IEC_INVERTED_LINES

// Reset line is available
#define IEC_HAS_RESET
                                                //            WIRING
                                                //  C64    DIN6    D32Pro          TFT
#define PIN_IEC_ATN             GPIO_NUM_4      //  ATN    3       A T-LED 32      10 (PURPLE)
#define PIN_IEC_CLK_IN          GPIO_NUM_5      //  CLK    4       A T-RST 33      8  (BROWN)
#define PIN_IEC_CLK_OUT         GPIO_NUM_5      //
#define PIN_IEC_DATA_IN         GPIO_NUM_6      //  DATA   5       T-CS 14         2  (BLACK)
#define PIN_IEC_DATA_OUT        GPIO_NUM_6      //
#define PIN_IEC_SRQ             GPIO_NUM_7      //  SRQ    1       T-DC 27         7  (ORANGE)
#define PIN_IEC_RESET           GPIO_NUM_15     //  RESET  6       A 32            N/C
                                                //  GND    2       GND             9  (GREY)


/* Modem/Parallel Switch */
#define PIN_MODEM_ENABLE        GPIO_NUM_19  // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_20 // High = UP9600 enabled

#endif // PINMAP_ESP32_1732S019
#endif // PINMAP_ESP32_1732S019_H