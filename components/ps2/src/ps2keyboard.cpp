#include "ps2keyboard.h"

static const char *TAG = "PS2_Keyboard";
static QueueHandle_t ps2_queue;
static TaskHandle_t ps2_task_handle = NULL;

PS2Keyboard::PS2Keyboard() {
    ps2_queue = xQueueCreate(32, sizeof(uint8_t));
}

void PS2Keyboard::start() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pin_bit_mask = (1ULL << PS2_CLK);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << PS2_DATA);
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PS2_CLK, ps2_isr_handler, (void *)this);

    xTaskCreatePinnedToCore(ps2_task, "ps2_task", 4096, this, 10, &ps2_task_handle, 0);
}

bool PS2Keyboard::available() {
    return uxQueueMessagesWaiting(ps2_queue) > 0;
}

uint8_t PS2Keyboard::read() {
    uint8_t data;
    if (xQueueReceive(ps2_queue, &data, portMAX_DELAY)) {
        return data;
    }
    return 0;
}

void IRAM_ATTR PS2Keyboard::ps2_isr_handler(void *arg) {
    static uint8_t bit_count = 0;
    static uint8_t data_byte = 0;
    static uint8_t parity = 0;
    static bool start_bit = false;

    if (!start_bit) {
        if (gpio_get_level(PS2_DATA) == 0) {
            start_bit = true;
            bit_count = 0;
            data_byte = 0;
            parity = 0;
        }
    } else {
        data_byte >>= 1;
        if (gpio_get_level(PS2_DATA)) {
            data_byte |= 0x80;
            parity ^= 1;
        }
        bit_count++;

        if (bit_count == 8) {
            bit_count++; // Skip parity
        } else if (bit_count == 9) {
            xQueueSendFromISR(ps2_queue, &data_byte, NULL);
            start_bit = false;
        }
    }
}

void PS2Keyboard::send_bit(bool bit) {
    gpio_set_direction(PS2_DATA, GPIO_MODE_OUTPUT);
    gpio_set_level(PS2_DATA, bit);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_direction(PS2_DATA, GPIO_MODE_INPUT);
}

void PS2Keyboard::write(uint8_t data) {
    uint8_t parity = 1;

    gpio_set_direction(PS2_CLK, GPIO_MODE_OUTPUT);
    gpio_set_level(PS2_CLK, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    gpio_set_direction(PS2_DATA, GPIO_MODE_OUTPUT);
    gpio_set_level(PS2_DATA, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    gpio_set_direction(PS2_CLK, GPIO_MODE_INPUT);

    send_bit(0);

    for (int i = 0; i < 8; i++) {
        send_bit(data & 0x01);
        parity ^= (data & 0x01);
        data >>= 1;
    }

    send_bit(parity);
    send_bit(1);

    gpio_set_direction(PS2_DATA, GPIO_MODE_INPUT);
}

void PS2Keyboard::write(const std::string data) {
    for (char c : data) {
        write(static_cast<uint8_t>(c));
    }
}

void PS2Keyboard::ps2_task(void *param) {
    PS2Keyboard *keyboard = static_cast<PS2Keyboard *>(param);

    while (true) {
        if (keyboard->available()) {
            uint8_t keycode = keyboard->read();
            ESP_LOGI(TAG, "Keycode: %02X", keycode);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
