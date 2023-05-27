#ifndef PINMAP_TTGO_T1_H
#define PINMAP_TTGO_T1_H

#ifdef PINMAP_TTGO_T1

/* SD Card */
// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#ifndef JTAG
#define PIN_CARD_DETECT 12 // fnSystem.h
#define PIN_CARD_DETECT_FIX 15 // fnSystem.h
#endif

#define PIN_SD_HOST_CS GPIO_NUM_13
#define PIN_SD_HOST_MISO GPIO_NUM_2
#define PIN_SD_HOST_MOSI GPIO_NUM_15
#define PIN_SD_HOST_SCK GPIO_NUM_14

/* UART */
#define PIN_UART0_RX 3 // fnUART.cpp
#define PIN_UART0_TX 1
#define PIN_UART1_RX 9
#define PIN_UART1_TX 10
#define PIN_UART2_RX 33
#define PIN_UART2_TX 21

/* Buttons */
#define PIN_BUTTON_A 0 // keys.cpp
#define PIN_BUTTON_B 34
#define PIN_BUTTON_C 14

/* LEDs */
#define PIN_LED_WIFI 22 // led.cpp
#define PIN_LED_BUS 22 // 4 FN
// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#ifndef JTAG 
#define PIN_LED_BT 22
#else
#define PIN_LED_BT 22 // LOLIN D32 PRO
#endif

/* Audio Output */
#define PIN_DAC1 25 // samlib.h

/* Commodore IEC Pins */
// CLK & DATA lines in/out are split between two pins
//#define IEC_SPLIT_LINES

// Line values are inverted (7406 Hex Inverter Buffer)
//#define IEC_INVERTED_LINES

// Reset line is available
#define IEC_HAS_RESET

// // CBM IEC Serial Port
// #define PIN_IEC_ATN         GPIO_NUM_39      // SIO 7  - CMD  - Command
// #define PIN_IEC_SRQ			GPIO_NUM_26      // SIO 13 - INT  - Interrupt
// #define PIN_IEC_RESET       GPIO_NUM_22      // SIO 9  - PROC - Proceed
//                                              // SIO 4 & 6 - GND - Ground

// // IEC_SPLIT_LINES
// #ifndef IEC_SPLIT_LINES
// // NOT SPLIT - Bidirectional Lines
// #define PIN_IEC_CLK_IN		GPIO_NUM_27      // SIO 1  - CKI  - Clock Input
// #define PIN_IEC_CLK_OUT	    GPIO_NUM_27      // SIO 1  - CKI  - Clock Input
// #define PIN_IEC_DATA_IN    	GPIO_NUM_21      // SIO 3  - DI   - Data Input
// #define PIN_IEC_DATA_OUT   	GPIO_NUM_21      // SIO 3  - DI   - Data Input
// #else
// // SPLIT - Seperate Input & Output lines
// #define PIN_IEC_CLK_IN		GPIO_NUM_32      // SIO 2  - CKO  - Clock Output
// #define PIN_IEC_CLK_OUT		GPIO_NUM_27      // SIO 1  - CKI  - Clock Input
// #define PIN_IEC_DATA_IN     GPIO_NUM_33      // SIO 5  - DO   - Data Output
// #define PIN_IEC_DATA_OUT    GPIO_NUM_21      // SIO 3  - DI   - Data Input
// #endif

#define PIN_IEC_RESET       GPIO_NUM_34
#define PIN_IEC_ATN         GPIO_NUM_32
#define PIN_IEC_CLK_IN		GPIO_NUM_23
#define PIN_IEC_CLK_OUT	    GPIO_NUM_23
#define PIN_IEC_DATA_IN    	GPIO_NUM_26
#define PIN_IEC_DATA_OUT   	GPIO_NUM_26
#define PIN_IEC_SRQ			GPIO_NUM_27

/* Modem/Parallel Switch */
#define PIN_MDMPAR_SW1       2  // High = Modem enabled
#define PIN_MDMPAR_SW2       15 // High = UP9600 enabled

/* I2C GPIO Expander */
#define PIN_GPIOX_SDA         GPIO_NUM_21
#define PIN_GPIOX_SCL         GPIO_NUM_22
#define PIN_GPIOX_INT         GPIO_NUM_34
//#define GPIOX_ADDRESS     0x20  // PCF8575
#define GPIOX_ADDRESS     0x24  // PCA9673
//#define GPIOX_SPEED       400   // PCF8575 - 400Khz
#define GPIOX_SPEED       1000  // PCA9673 - 1000Khz / 1Mhz

#endif // PINMAP_TTGO_T1
#endif // PINMAP_TTGO_T1_H