#ifndef PINMAP_WAVESHARE_P4_MODULE_DEV_KIT_H
#define PINMAP_WAVESHARE_P4_MODULE_DEV_KIT_H

// https://www.waveshare.com/wiki/ESP32-P4-Module-DEV-KIT-StartPage
// https://files.waveshare.com/wiki/ESP32-P4-Module-DEV-KIT/ESP32-P4-Module-DEV-KIT.pdf
// https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
// https://www.espressif.com/sites/default/files/documentation/esp32-p4_datasheet_en.pdf

#ifdef PINMAP_WAVESHARE_P4_MODULE_DEV_KIT

// ESP32-S3-WROOM-1-N16R8
#define FLASH_SIZE              16
#define PSRAM_SIZE              32

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_NC // fnSystem.h
//#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC // fnSystem.h

#define SDMMC_HOST_WIDTH        4           // 4-bit mode
//#define SDMMC_PULL_UP			true

#define PIN_SD_HOST_CLK         GPIO_NUM_43
#define PIN_SD_HOST_CMD         GPIO_NUM_44
#define PIN_SD_HOST_D0          GPIO_NUM_39
#define PIN_SD_HOST_D1          GPIO_NUM_40
#define PIN_SD_HOST_D2          GPIO_NUM_41
#define PIN_SD_HOST_D3          GPIO_NUM_42
#define PIN_SD_HOST_WP          GPIO_NUM_NC

/* UART */
#define PIN_UART0_RX            GPIO_NUM_38  // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_37
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_NC
#define PIN_UART2_TX            GPIO_NUM_NC

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_20
#define PIN_BUTTON_B            GPIO_NUM_6
#define PIN_BUTTON_C            GPIO_NUM_5
#define PIN_BUTTON_D            GPIO_NUM_23
#define PIN_BUTTON_E            GPIO_NUM_33

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_46  // led.cpp
#define PIN_LED_BUS             GPIO_NUM_27
#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_RGB             GPIO_NUM_45

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_26
#define PIN_I2S                 GPIO_NUM_48

/* I2C GPIO Expander */
#ifdef PARALLEL_BUS
#define PIN_GPIOX_SDA           GPIO_NUM_NC
#define PIN_GPIOX_SCL           GPIO_NUM_NC
#define PIN_GPIOX_INT           GPIO_NUM_NC
#define GPIOX_ADDRESS           0x20  // PCF8575
//#define GPIOX_ADDRESS           0x24  // PCA9673
#define GPIOX_SPEED             400   // PCF8575 - 400Khz
//#define GPIOX_SPEED             1000  // PCA9673 - 1000Khz / 1Mhz
#endif

/* Commodore IEC Pins */
// CLK & DATA lines in/out are split between two pins
//#define IEC_SPLIT_LINES

// Line values are inverted (7406 Hex Inverter Buffer)
//#define IEC_INVERTED_LINES

// Reset line is available
#define IEC_HAS_RESET
                                                //    WIRING
                                                //  C64    DIN6
#define PIN_IEC_ATN             GPIO_NUM_7      //  ATN    3
#define PIN_IEC_CLK_IN          GPIO_NUM_21     //  CLK    4
#define PIN_IEC_CLK_OUT         GPIO_NUM_21     //
#define PIN_IEC_DATA_IN         GPIO_NUM_22     //  DATA   5
#define PIN_IEC_DATA_OUT        GPIO_NUM_22     //
#define PIN_IEC_SRQ             GPIO_NUM_4      //  SRQ    1
#define PIN_IEC_RESET           GPIO_NUM_8      //  RESET  6
//                            SIDE OF SD SLOT   //  GND    2

/* Modem/Parallel Switch */
/* Unused with Nugget    */
#define PIN_MODEM_ENABLE        GPIO_NUM_NC // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_NC // High = UP9600 enabled

#endif // PINMAP_WAVESHARE_P4_MODULE_DEV_KIT
#endif // PINMAP_WAVESHARE_P4_MODULE_DEV_KIT_H