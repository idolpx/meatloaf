#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string>

#define PS2_CLK GPIO_NUM_18  // Change as needed
#define PS2_DATA GPIO_NUM_19 // Change as needed

class PS2Keyboard {
public:
    PS2Keyboard();
    void start();
    bool available();
    uint8_t read();
    void write(uint8_t data);
    void write(const std::string data);

private:
    static void IRAM_ATTR ps2_isr_handler(void *arg);
    void send_bit(bool bit);
    static void ps2_task(void *param);
};

extern PS2Keyboard keyboard;
#endif // PS2_KEYBOARD_H
