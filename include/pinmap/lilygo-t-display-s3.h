
#ifndef PINMAP_LILYGO_T_DISPLAY_S3_H
#define PINMAP_LILYGO_T_DISPLAY_S3_H

// https://lilygo.cc/products/t-display-s3
// https://github.com/Xinyuan-LilyGO/T-Display-S3
// https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/image/T-DISPLAY-S3.jpg
// https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/schematic/T_Display_S3.pdf
// https://www.espressif.com.cn/sites/default/files/documentation/esp32-s3_datasheet_en.pdf

#ifdef PINMAP_LILYGO_T_DISPLAY_S3

// ESP32-S3R8
#define FLASH_SIZE              16
#define PSRAM_SIZE              8

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_NC // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC // fnSystem.h

#define PIN_SD_HOST_CS          GPIO_NUM_10
#define PIN_SD_HOST_MISO        GPIO_NUM_11
#define PIN_SD_HOST_MOSI        GPIO_NUM_13
#define PIN_SD_HOST_SCK         GPIO_NUM_12

/* UART */
#define PIN_UART0_RX            GPIO_NUM_44  // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_43
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_NC
#define PIN_UART2_TX            GPIO_NUM_NC

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_0  // keys.cpp
#define PIN_BUTTON_B            GPIO_NUM_14
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_1   // led.cpp
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_RGB             GPIO_NUM_2

/* LCD */
// 1.9in ST7789 - 170x320
#define PIN_LCD_POWER           GPIO_NUM_15
#define PIN_LCD_BL              GPIO_NUM_38
#define PIN_LCD_D0              GPIO_NUM_39
#define PIN_LCD_D1              GPIO_NUM_40
#define PIN_LCD_D2              GPIO_NUM_41
#define PIN_LCD_D3              GPIO_NUM_42
#define PIN_LCD_D4              GPIO_NUM_45
#define PIN_LCD_D5              GPIO_NUM_46
#define PIN_LCD_D6              GPIO_NUM_47
#define PIN_LCD_D7              GPIO_NUM_48
#define PIN_LCD_WR              GPIO_NUM_08
#define PIN_LCD_RD              GPIO_NUM_09
#define PIN_LCD_DC              GPIO_NUM_07
#define PIN_LCD_CS              GPIO_NUM_06
#define PIN_LCD_RES             GPIO_NUM_05

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
#define PIN_IEC_ATN             GPIO_NUM_3      //  ATN    3   
#define PIN_IEC_CLK_IN          GPIO_NUM_18     //  CLK    4   
#define PIN_IEC_CLK_OUT         GPIO_NUM_18     //
#define PIN_IEC_DATA_IN         GPIO_NUM_17     //  DATA   5   
#define PIN_IEC_DATA_OUT        GPIO_NUM_17     //
#define PIN_IEC_SRQ             GPIO_NUM_21     //  SRQ    1   
#define PIN_IEC_RESET           GPIO_NUM_16     //  RESET  6   
                                                //  GND    2   


/* Modem/Parallel Switch */
#define PIN_MODEM_ENABLE        GPIO_NUM_NC  // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_NC  // High = UP9600 enabled

#endif // PINMAP_LILYGO_T_DISPLAY_S3
#endif // PINMAP_LILYGO_T_DISPLAY_S3_H