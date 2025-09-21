#ifndef PINMAP_PETDISKMAXV2_H
#define PINMAP_PETDISKMAXV2_H

// https://bitfixer.com/product/petdisk-max/
// https://www.espressif.com.cn/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf

#ifdef PINMAP_PETDISKMAXV2

// ESP32-WROOM-32
#define FLASH_SIZE              4
//#define PSRAM_SIZE              8

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_12 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_15 // fnSystem.h

#define PIN_SD_HOST_CS          GPIO_NUM_4
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
#define PIN_BUTTON_A            GPIO_NUM_NC  // keys.cpp
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_2 // led.cpp
#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_RGB             GPIO_NUM_NC

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_NC // samlib.h


#define PIN_GPIB_ATN         GPIO_NUM_5  // Attention
#define PIN_GPIB_DAV         GPIO_NUM_21 // Data Valid
#define PIN_GPIB_NRFD        GPIO_NUM_17 // Not Ready For Data
#define PIN_GPIB_NDAC        GPIO_NUM_16 // No Data Accepted
#define PIN_GPIB_EOI         GPIO_NUM_22 // End Or Identify

#define PIN_GPIB_REN         GPIO_NUM_NC // Remote Enable (IEEE-488 enabled if pulled low)
#define PIN_GPIB_SRQ         GPIO_NUM_15 // Service Request
#define PIN_GPIB_IFC         GPIO_NUM_16 // Interface Clear (RESET)

#define PIN_GPIB_DATADIR     GPIO_NUM_15

#define PIN_GPIB_DATA0       GPIO_NUM_32
#define PIN_GPIB_DATA1       GPIO_NUM_33
#define PIN_GPIB_DATA2       GPIO_NUM_25
#define PIN_GPIB_DATA3       GPIO_NUM_26
#define PIN_GPIB_DATA4       GPIO_NUM_27
#define PIN_GPIB_DATA5       GPIO_NUM_14
#define PIN_GPIB_DATA6       GPIO_NUM_12
#define PIN_GPIB_DATA7       GPIO_NUM_13

// The eight DIO lines carry the data bytes.
// The NRFD, DAV and NDAC lines are used to perform handshaking.
// EOI, ATN, SRQ and REN are control lines.
// IFC is the RESET line for all devices.

#endif // PINMAP_PETDISKMAXV2
#endif // PINMAP_PETDISKMAXV2_H