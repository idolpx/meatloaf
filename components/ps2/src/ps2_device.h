#ifndef PS2_DEVICE_H
#define PS2_DEVICE_H

#include <initializer_list>
#include <stack>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <stdint.h>

#include <nvs_flash.h>
#include <string>
#include <functional>

#define NOP() asm volatile("nop")
#define HIGH 0x1
#define LOW 0x0

// Unomment following line to enable debug messages on the PS2DEV module
//#define _ps2dev_DEBUG_

namespace ps2dev
{

  // THIS SECTION DEFINES THE FUNCTIONS USED BELOW AS IMPLEMENTED IN THE ARDUINO CORE FOR ESP32
  // THE CODE ON THIS FILE HAS BEEN PORTED FROM AN ARDUINO-IDE PROJECT SO THIS IS NECESSARY
  // I KNOW IT'S NOT IDEAL BUT I DO NOT HAVE TIME TO MODIFY ALL NOR THE PATIENCE
  // ALSO THIS IS VERY TIME SENSITIVE CODE SO BETTER LEFT ALONE

  BaseType_t xTaskCreateUniversal(TaskFunction_t pxTaskCode,
                                  const char *const pcName,
                                  const uint32_t usStackDepth,
                                  void *const pvParameters,
                                  UBaseType_t uxPriority,
                                  TaskHandle_t *const pxCreatedTask,
                                  const BaseType_t xCoreID) {
  #ifndef CONFIG_FREERTOS_UNICORE
      if (xCoreID >= 0 && xCoreID < 2) {
          return xTaskCreatePinnedToCore(pxTaskCode, pcName, usStackDepth,
                                        pvParameters, uxPriority, pxCreatedTask,
                                        xCoreID);
      } else {
  #endif
          return xTaskCreate(pxTaskCode, pcName, usStackDepth, pvParameters,
                            uxPriority, pxCreatedTask);
  #ifndef CONFIG_FREERTOS_UNICORE
      }
  #endif
  }

  void delay(uint32_t ms) { vTaskDelay(ms / portTICK_PERIOD_MS); }

  unsigned long IRAM_ATTR millis() {
      return (unsigned long)(esp_timer_get_time() / 1000ULL);
  }

  unsigned long IRAM_ATTR micros() {
      return (unsigned long)(esp_timer_get_time());
  }

  void IRAM_ATTR delayMicroseconds(uint32_t us) {
      uint32_t m = micros();
      if (us) {
          uint32_t e = (m + us);
          if (m > e) {  // overflow
              while (micros() > e) {
                  NOP();
              }
          }
          while (micros() < e) {
              NOP();
          }
      }
  }

  // END OF ARDUINO CORE ADAPTATION

  // Time per clock should be 60 to 100 microseconds according to PS/2 specifications.
  // Thus, half period should be 30 to 50 microseconds.
  const uint32_t CLK_HALF_PERIOD_MICROS = 40;
  const uint32_t CLK_QUATER_PERIOD_MICROS = CLK_HALF_PERIOD_MICROS / 2;
  // I could not find any specification of time between bytes from the PS/2 specification.
  // Based on observation of the mouse signal waveform using an oscilloscope, there appears to be an interval of 1 to 2 clock cycles.
  // ref. https://youtu.be/UqRDLWGLCEk
  const uint32_t BYTE_INTERVAL_MICROS = 500; // in v0.4 was OK: 500, change if not working and you know what you're doing.
  const int PACKET_QUEUE_LENGTH = 20;
  const UBaseType_t DEFAULT_TASK_PRIORITY = 10;
  //const BaseType_t DEFAULT_TASK_CORE = APP_CPU_NUM;
  const BaseType_t DEFAULT_TASK_CORE = 0;
  const BaseType_t DEFAULT_TASK_CORE_MOUSE = 1;
  // The device should check for "HOST_REQUEST_TO_SEND" at a interval not exceeding 10 milliseconds.
  const uint32_t INTERVAL_CHECKING_HOST_SEND_REQUEST_MILLIS = 9;
  const uint32_t MOUSE_CLICK_PRESSING_DURATION_MILLIS = 100;

  class PS2Packet
  {
  public:
    uint8_t len;
    uint8_t data[16];
  };

  class PS2Device
  {
    void ps2_isr_handler(void *arg);
    void ps2_task(void *param);

  public:
    PS2Device(gpio_num_t clk, gpio_num_t data);

    enum class BusState
    {
      IDLE,
      COMMUNICATION_INHIBITED,
      HOST_REQUEST_TO_SEND,
    };

    typedef std::function<void(uint8_t data)> ps2_data_callback_t;
    ps2_data_callback_t _ps2_data_callback;

    void set_ps2_data_callback(ps2_data_callback_t callback) {
        _ps2_data_callback = callback;
    }

    void config(UBaseType_t task_priority, BaseType_t task_core);
    void begin(BaseType_t core = DEFAULT_TASK_CORE);
    int write(unsigned char data);
    int write_wait_idle(uint8_t data, uint64_t timeout_micros = 1500);
    int read(unsigned char *data, uint64_t timeout_ms = 0);
    virtual int reply_to_host(uint8_t host_cmd) = 0;
    BusState get_bus_state();
    SemaphoreHandle_t get_bus_mutex_handle();
    QueueHandle_t get_packet_queue_handle();
    int send_packet(PS2Packet *packet);

  protected:
    gpio_num_t _ps2clk;
    gpio_num_t _ps2data;
    UBaseType_t _config_task_priority = DEFAULT_TASK_PRIORITY;
    BaseType_t _config_task_core = DEFAULT_TASK_CORE;
    TaskHandle_t _task_process_host_request;
    TaskHandle_t _task_send_packet;
    QueueHandle_t _queue_packet;
    SemaphoreHandle_t _mutex_bus;
    void golo(gpio_num_t pin);
    void gohi(gpio_num_t pin);
    void ack();
  
  private:
    QueueHandle_t ps2_queue;
    TaskHandle_t ps2_task_handle = NULL;
  };
} // namespace ps2dev

#endif // PS2_DEVICE_H