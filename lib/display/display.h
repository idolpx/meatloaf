
#ifndef MEATLOAF_DISPLAY_H
#define MEATLOAF_DISPLAY_H

#include "freertos/FreeRTOS.h"
#include <freertos/queue.h>
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include <stdio.h>
#include <string.h>

#include "../../include/pinmap.h"
#include "../../include/debug.h"
typedef struct {
    union {
        struct {
            union {
                uint8_t r;
                uint8_t red;
            };

            union {
                uint8_t g;
                uint8_t green;
            };

            union {
                uint8_t b;
                uint8_t blue;
            };
        };

        uint8_t raw[3];
        uint32_t num;
    };
} CRGB;

typedef struct {
    spi_host_device_t host;
    spi_device_handle_t spi;
    int dma_chan;
    spi_device_interface_config_t devcfg;
    spi_bus_config_t buscfg;
} spi_settings_t;

typedef enum {
    WS2812B = 0,
    WS2815,
} led_strip_model_t;

enum Mode {
  MODE_STATUS  = -1,
  MODE_IDLE    = 0,
  MODE_SEND    = 1,
  MODE_RECEIVE = 2,
  MODE_CUSTOM  = 3
};

//static QueueHandle_t display_evt_queue = NULL;
    
class Display
{
  private:
    //BaseType_t m_task_handle;
    uint8_t m_statusCode = 0;
    uint8_t m_direction = 0; // 0 = left to right (SEND), 1 = right to left (RECEIVE)

    esp_err_t init(int pin, led_strip_model_t model, int num_of_leds, CRGB **led_buffer_ptr);

  public:
    Mode mode = MODE_IDLE;
    uint16_t speed = 300;
    uint8_t progress = 100;

    void start(void);
    void service();
    esp_err_t update();

    void idle(void) { mode = MODE_IDLE; };
    void send(void) { mode = MODE_SEND; m_direction = 0; };
    void receive(void) { mode = MODE_RECEIVE; m_direction = 1; };
    void status(uint8_t code) { 
        mode = MODE_STATUS;
        m_statusCode = code;
    };

    void show_progress();
    void show_activity();
    void blink();
    void fill_all(CRGB color);

    void meatloaf();
};

extern Display DISPLAY;

#endif // MEATLOAF_DISPLAY_H