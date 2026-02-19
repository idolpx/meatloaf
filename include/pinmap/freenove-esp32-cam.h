/*
 *  Tested on the Freenove [FNK0060] ESP32 WROVER Board v3
 *  https://docs.freenove.com/projects/fnk0060/en/latest/
 *  https://www.amazon.com/Freenove-Dual-core-Microcontroller-Wireless-Projects/dp/B0CJJHXD1W
 *
 *  The kit comes with a camera module, but this configuration requires that you leave it disconnected.
 *
 *  No support for live updates.  Two issues:  ran out of mail program space in flash, and updates don't support SDMMC.
 */

#ifndef PINMAP_FREENOVE_ESP32_CAM_H
#define PINMAP_FREENOVE_ESP32_CAM_H

#ifdef PINMAP_FREENOVE_ESP32_CAM

// Freenove ESP32-WROVER-E CAM Board
#define FLASH_SIZE              4
#define PSRAM_SIZE              8           // Note: Only 4MB is directly mappable on ESP32; remaining requires himem API

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_NC // fnSystem.h
//#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC // fnSystem.h

#define SDMMC_HOST_WIDTH        1           // 1-bit mode (D0) to avoid conflict with LED (D1) and Strapping (D2)
//#define SDMMC_PULL_UP			true

#define PIN_SD_HOST_CLK         GPIO_NUM_14 // ADC2_CH5/HSPI_CLK/TOUCH6/SDMMC MTDI
#define PIN_SD_HOST_CMD         GPIO_NUM_15 // ADC2_CH3/Strapping MTDO/TOUCH3/HSPI_CS/SDMMC CMD
#define PIN_SD_HOST_D0          GPIO_NUM_2  // ADC2_CH2/Boot Mode/TOUCH2/LED IO2/SDMMC DATA
#define PIN_SD_HOST_D1          GPIO_NUM_NC  // ADC2_CH0/CAM_Y2/TOUCH0
#define PIN_SD_HOST_D2          GPIO_NUM_NC // ADC2_CH5/HSPI_MISO/TOUCH5/Strapping MTDI
#define PIN_SD_HOST_D3          GPIO_NUM_NC // ADC2_CH4/HSPI_MOSI/TOUCH4
#define PIN_SD_HOST_WP          GPIO_NUM_NC

/* UART */
#define PIN_UART0_RX            GPIO_NUM_3
#define PIN_UART0_TX            GPIO_NUM_1
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_NC
#define PIN_UART2_TX            GPIO_NUM_NC

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_0  // BOOT button
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_NC  // Onboard LED (shared with MISO, might conflict if SD is used)
#define PIN_LED_BUS             GPIO_NUM_33 // Example, adjust as needed
#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_RGB             GPIO_NUM_NC

/* Camera Header (Reserved Pins) */
// D0-D7, XCLK, PCLK, VSYNC, HREF, SDA, SCL, RESET, PWDN
// Avoid using these if camera is connected or if pins are dedicated


// Reset line is available
#define IEC_HAS_RESET

/* Commodore IEC Pins (Example Mapping - Adjust based on available free pins) */
#define PIN_IEC_ATN             GPIO_NUM_19 // Moved from 12 (Strapping Pin)
#define PIN_IEC_CLK_IN          GPIO_NUM_21 // Moved from 4 (Avoids Flash LED)
#define PIN_IEC_CLK_OUT         GPIO_NUM_21
#define PIN_IEC_DATA_IN         GPIO_NUM_22 // Moved from 5 (Grouping with CLK)
#define PIN_IEC_DATA_OUT        GPIO_NUM_22
#define PIN_IEC_SRQ             GPIO_NUM_18 // CAM_Y4 / VSPI_CLK
#define PIN_IEC_RESET           GPIO_NUM_23 // CAM_Y8 (Input Only)

#endif // PINMAP_FREENOVE_ESP32_CAM
#endif // PINMAP_FREENOVE_ESP32_CAM_H