
#ifndef PINMAP_LOLIN_D32_PRO_H
#define PINMAP_LOLIN_D32_PRO_H

// https://www.wemos.cc/en/latest/d32/d32_pro.html
// https://www.wemos.cc/en/latest/_static/files/sch_d32_pro_v2.0.0.pdf
// https://www.espressif.com.cn/sites/default/files/documentation/esp32-wrover-e_esp32-wrover-ie_datasheet_en.pdf

#ifdef PINMAP_LOLIN_D32_PRO

// ESP32-WROVER-E-N16R8
#define FLASH_SIZE              16
#define PSRAM_SIZE              8

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_12 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_15 // fnSystem.h

#define PIN_SD_HOST_CS          GPIO_NUM_4  // LOLIN D32 Pro
#define PIN_SD_HOST_MISO        GPIO_NUM_19
#define PIN_SD_HOST_MOSI        GPIO_NUM_23
#define PIN_SD_HOST_SCK         GPIO_NUM_18

/* UART */
#define PIN_UART0_RX            GPIO_NUM_3  // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_1
#define PIN_UART1_RX            GPIO_NUM_9
#define PIN_UART1_TX            GPIO_NUM_10
#define PIN_UART2_RX            GPIO_NUM_NC
#define PIN_UART2_TX            GPIO_NUM_NC

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_0  // keys.cpp
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_14

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_2 // led.cpp
#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_BUS             GPIO_NUM_5
#define PIN_LED_RGB             GPIO_NUM_13

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_25 // samlib.h

/* Parallel cable */
//#define PIN_XRA1405_CS          GPIO_NUM_21
//#define PIN_PARALLEL_PC2        GPIO_NUM_27
//#define PIN_PARALLEL_FLAG2      GPIO_NUM_22

/* Commodore IEC Pins */
// CLK & DATA lines in/out are split between two pins
//#define IEC_SPLIT_LINES

// Line values are inverted (7406 Hex Inverter Buffer)
//#define IEC_INVERTED_LINES

// Reset line is available
#define IEC_HAS_RESET

#define PIN_IEC_ATN             GPIO_NUM_32
#define PIN_IEC_CLK_IN          GPIO_NUM_33
#define PIN_IEC_CLK_OUT         GPIO_NUM_33
#define PIN_IEC_DATA_IN         GPIO_NUM_25
#define PIN_IEC_DATA_OUT        GPIO_NUM_25
#define PIN_IEC_SRQ             GPIO_NUM_26
#define PIN_IEC_RESET           GPIO_NUM_34
// GND - Be sure to connect GND of the IEC cable to GND on the ESP module


/* Modem/Parallel Switch */
#define PIN_MODEM_ENABLE        GPIO_NUM_2  // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_15 // High = UP9600 enabled


/* Cassette */
#define PIN_CASS_MOTOR          GPIO_NUM_39         // VN Cassette Motor is active if high
#define PIN_CASS_SENSE          PIN_IEC_CLK_IN      // key has been pressed if low
#define PIN_CASS_WRITE          PIN_IEC_DATA_IN     // DATA IN from 64
#define PIN_CASS_READ           PIN_IEC_SRQ         // DATA OUT to 64


#endif // PINMAP_LOLIN_D32_PRO
#endif // PINMAP_LOLIN_D32_PRO_H
