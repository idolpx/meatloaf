
#include <freertos/queue.h>

#include <I2Cbus.hpp>

#include "../include/debug.h"

/* PCF8575 port bits */
#define P00  0
#define P01  1
#define P02  2
#define P03  3
#define P04  4
#define P05  5
#define P06  6
#define P07  7
#define P10  8
#define P11  9
#define P12  10
#define P13  11
#define P14  12
#define P15  13
#define P16  14
#define P17  15

// #define P00(di)  (di & bit(0)>0)?HIGH:LOW;
// #define P01(di)  (di & bit(1)>0)?HIGH:LOW;
// #define P02(di)  (di & bit(2)>0)?HIGH:LOW;
// #define P03(di)  (di & bit(3)>0)?HIGH:LOW;
// #define P04(di)  (di & bit(4)>0)?HIGH:LOW;
// #define P05(di)  (di & bit(5)>0)?HIGH:LOW;
// #define P06(di)  (di & bit(6)>0)?HIGH:LOW;
// #define P07(di)  (di & bit(7)>0)?HIGH:LOW;
// #define P08(di)  (di & bit(8)>0)?HIGH:LOW;
// #define P09(di)  (di & bit(9)>0)?HIGH:LOW;
// #define P10(di)  (di & bit(10)>0)?HIGH:LOW;
// #define P11(di)  (di & bit(11)>0)?HIGH:LOW;
// #define P12(di)  (di & bit(12)>0)?HIGH:LOW;
// #define P13(di)  (di & bit(13)>0)?HIGH:LOW;
// #define P14(di)  (di & bit(14)>0)?HIGH:LOW;
// #define P15(di)  (di & bit(15)>0)?HIGH:LOW;


/* User Port to pin mapping */
#define FLAG2  P07  // B
#define CNT1   P06  // 4
#define SP1    P05  // 5
#define CNT2   P04  // 6
#define SP2    P03  // 7
#define PC2    P02  // 8
#define ATN    P01  // 9
#define PA2    P00  // M

#define PB0    P10   // C
#define PB1    P11   // D
#define PB2    P12   // E
#define PB3    P13   // F
#define PB4    P14   // H - G
#define PB5    P15   // J - H
#define PB6    P16   // K - I
#define PB7    P17   // L - J



#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0')

static xQueueHandle ml_parallel_evt_queue = NULL;

I2C_t& myI2C = i2c0;  // i2c0 and i2c1 are the default objects

static void IRAM_ATTR ml_parallel_isr_handler(void* arg)
{
    // Generic default interrupt handler
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(ml_parallel_evt_queue, &gpio_num, NULL);
    //Debug_printf("INTERRUPT ON GPIO: %d", arg);
}

static void ml_parallel_intr_task(void* arg)
{
    uint32_t io_num;
    uint8_t buffer[2];

    // Setup default pin modes
    myI2C.readByte( 0x20, 0x00, buffer );

    while (1) {
        if(xQueueReceive(ml_parallel_evt_queue, &io_num, portMAX_DELAY)) 
        {
            // Read I/O lines
            i2c0.readBytes( 0x20, 0x00, 2, buffer );
            Debug_printv(BYTE_TO_BINARY_PATTERN " " BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(buffer[0]), BYTE_TO_BINARY(buffer[1]));
        }
    }
    
    myI2C.close();
    vTaskDelay(portMAX_DELAY);
}


void parallel_setup ()
{
    // Setup i2c device
    myI2C.begin(GPIO_NUM_21, GPIO_NUM_22, 400000);
    myI2C.setTimeout(10);
    myI2C.scanner();
    
    // Create a queue to handle parallel event from ISR
    ml_parallel_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // Start task
    xTaskCreate(ml_parallel_intr_task, "ml_parallel_intr_task", 2048, NULL, 10, NULL);

    // Setup interrupt for paralellel port
    gpio_config_t io_conf = {
        .pin_bit_mask = ( 1ULL << GPIO_NUM_19 ),    // bit mask of the pins that you want to set
        .mode = GPIO_MODE_INPUT,                    // set as input mode
        .pull_up_en = GPIO_PULLUP_DISABLE,          // disable pull-up mode
        .pull_down_en = GPIO_PULLDOWN_DISABLE,      // disable pull-down mode
        .intr_type = GPIO_INTR_NEGEDGE              // interrupt of falling edge
    };
    //configure GPIO with the given settings
    gpio_config(&io_conf);
    gpio_isr_handler_add((gpio_num_t)GPIO_NUM_19, ml_parallel_isr_handler, NULL);    
} 