
#ifndef PINMAP_POCKET_DONGLE_S3_H
#define PINMAP_POCKET_DONGLE_S3_H

// https://lilygo.cc/products/t-display-s3
// https://github.com/Xinyuan-LilyGO/T-Display-S3
// https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/image/T-DISPLAY-S3.jpg
// https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/schematic/T_Display_S3.pdf
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

// /* SD Card */
// #define PIN_CARD_DETECT         GPIO_NUM_NC // fnSystem.h
// //#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC // fnSystem.h

// #define SDMMC_HOST_WIDTH        1           // 1-bit mode (D0) to avoid conflict with LED (D1) and Strapping (D2)
// //#define SDMMC_PULL_UP			true

// #define PIN_SD_HOST_CLK         GPIO_NUM_17 // ADC2_CH5/HSPI_CLK/TOUCH6/SDMMC MTDI
// #define PIN_SD_HOST_CMD         GPIO_NUM_18 // ADC2_CH3/Strapping MTDO/TOUCH3/HSPI_CS/SDMMC CMD
// #define PIN_SD_HOST_D0          GPIO_NUM_16 // ADC2_CH2/Boot Mode/TOUCH2/LED IO2/SDMMC DATA
// #define PIN_SD_HOST_D1          GPIO_NUM_NC  // ADC2_CH0/CAM_Y2/TOUCH0
// #define PIN_SD_HOST_D2          GPIO_NUM_NC // ADC2_CH5/HSPI_MISO/TOUCH5/Strapping MTDI
// #define PIN_SD_HOST_D3          GPIO_NUM_47 // ADC2_CH4/HSPI_MOSI/TOUCH4
// #define PIN_SD_HOST_WP          GPIO_NUM_NC
// Clk_pin: gpio17 Cmd_pin: gpio18 # Mosi Data0_pin: gpio16 # Miso Data3_pin: gpio47 # Cs

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
# define LCD_HOST SPI2_HOST
# define PIN_LCD_SCLK           GPIO_NUM_10
# define PIN_LCD_MOSI           GPIO_NUM_11
# define PIN_LCD_CS             GPIO_NUM_12
# define PIN_LCD_DC             GPIO_NUM_13
# define PIN_LCD_RST            GPIO_NUM_14

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