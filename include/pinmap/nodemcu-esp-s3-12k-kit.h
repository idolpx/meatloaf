
#ifndef PINMAP_NODEMCU_S3_12K_H
#define PINMAP_NODEMCU_S3_12K_H

// https://www.waveshare.com/wiki/NodeMCU-ESP-S3-12K-Kit
// https://www.waveshare.com/w/upload/6/68/Nodemcu-esp-s3-12k-kit_schematic.pdf
// https://files.waveshare.com/upload/b/bd/Esp32-s3_datasheet_en.pdf
// https://www.waveshare.com/w/upload/6/6b/Esp-s3-12k_module_datasheet_v1.0.0.pdf

#ifdef PINMAP_NODEMCU_S3_12K

// ESP32-S3-12K
#define FLASH_SIZE              8
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
#define PIN_LED_WIFI            GPIO_NUM_5   // led.cpp
#define PIN_LED_BUS             GPIO_NUM_38
#define PIN_LED_BT              GPIO_NUM_39
#define PIN_LED_R               GPIO_NUM_5
#define PIN_LED_G               GPIO_NUM_6
#define PIN_LED_B               GPIO_NUM_7

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_NC // samlib.h


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
#define PIN_IEC_CLK_IN          GPIO_NUM_11     //  CLK    4
#define PIN_IEC_CLK_OUT         GPIO_NUM_11     //
#define PIN_IEC_DATA_IN         GPIO_NUM_13     //  DATA   5
#define PIN_IEC_DATA_OUT        GPIO_NUM_13     //
#define PIN_IEC_SRQ             GPIO_NUM_14     //  SRQ    1
#define PIN_IEC_RESET           GPIO_NUM_8      //  RESET  6
                                                //  GND    2


/* Modem/Parallel Switch */
#define PIN_MODEM_ENABLE        GPIO_NUM_2  // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_15 // High = UP9600 enabled

/* I2C GPIO Expander */
#define PIN_GPIOX_SDA           GPIO_NUM_9
#define PIN_GPIOX_SCL           GPIO_NUM_10
#define PIN_GPIOX_INT           GPIO_NUM_39
#define GPIOX_ADDRESS           0x20  // PCF8575
//#define GPIOX_ADDRESS           0x24  // PCA9673
#define GPIOX_SPEED             400   // PCF8575 - 400Khz
//#define GPIOX_SPEED             1000  // PCA9673 - 1000Khz / 1Mhz

#endif // PINMAP_NODEMCU_S3_12K
#endif // PINMAP_NODEMCU_S3_12K_H