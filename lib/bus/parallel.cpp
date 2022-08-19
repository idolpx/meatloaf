
#include "parallel.h"

#include <freertos/queue.h>

#include <I2Cbus.hpp>

#include "iec.h"
#include "../../include/debug.h"


parallelBus PARALLEL;

I2C_t& myI2C = i2c0;  // i2c0 and i2c1 are the default objects

static xQueueHandle ml_parallel_evt_queue = NULL;

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

    // Setup default pin modes
    PARALLEL.readByte();

    while (1) {
        if(xQueueReceive(ml_parallel_evt_queue, &io_num, portMAX_DELAY)) 
        {
            // Read I/O lines
            uint8_t buffer[2];
            myI2C.readBytes( 0x20, 0x00, 2, buffer );
            PARALLEL.flags = buffer[0];
            PARALLEL.data = buffer[1];
                   
            if ( PARALLEL.status( PA2 ) )
            {
                PARALLEL.mode = MODE_RECEIVE;
                Debug_printv("receive <<< " BYTE_TO_BINARY_PATTERN " " BYTE_TO_BINARY_PATTERN " (%0.2d)", BYTE_TO_BINARY(PARALLEL.flags), BYTE_TO_BINARY(PARALLEL.data), PARALLEL.data);
            }
            else
            {
                PARALLEL.mode = MODE_SEND;
                Debug_printv("send >>> " BYTE_TO_BINARY_PATTERN " " BYTE_TO_BINARY_PATTERN " (%0.2d)", BYTE_TO_BINARY(PARALLEL.flags), BYTE_TO_BINARY(PARALLEL.data), PARALLEL.data);
            }


            if ( PARALLEL.status(ATN) )
            {
                // Set Mode
                if ( IEC.data.secondary == IEC_OPEN || IEC.data.secondary == IEC_REOPEN )
                {
                    IEC.protocol.flags xor_eq DOLPHIN_ACTIVE;
                    Debug_printv("dolphindos");
                }
            }

        }
    }
    
    myI2C.close();
    vTaskDelay(portMAX_DELAY);
}


void parallelBus::setup ()
{
    // Setup i2c device
    myI2C.begin(GPIO_NUM_21, GPIO_NUM_22, 400000); // 400Khz Default
    //myI2C.begin(GPIO_NUM_21, GPIO_NUM_22, 800000); // 800Khz Overclocked
    //myI2C.begin(GPIO_NUM_21, GPIO_NUM_22, 1000000); // 1Mhz Overclocked
    myI2C.setTimeout(10);
    //myI2C.scanner();
    
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

uint8_t parallelBus::readByte()
{
    // ISR receives byte automatically

    // uint8_t buffer[2];
    // i2c0.readBytes( 0x20, 0x00, 2, buffer );
    // this->flags = buffer[0];
    // this->data = buffer[1];

    // Set NMI FLAG2 to confirm byte was received
    flags xor_eq (1 << FLAG2);
    this->sendByte( this->data );

    return this->data;
}

void parallelBus::sendByte( uint8_t byte )
{
    // Set NMI FLAG2 to confirm byte was sent
    flags xor_eq (1 << FLAG2);

    uint8_t buffer[2];
    buffer[0] = this->flags;
    buffer[1] = this->data;

    // Wait until 
    while ( this->mode != MODE_SEND );

    i2c0.writeBytes( 0x20, 0x00, 2, buffer );
}

bool parallelBus::status( user_port_pin_t pin )
{
    if ( pin < 8 ) 
        return ( this->flags bitand pin );
    
    return ( this->data bitand pin );
}
