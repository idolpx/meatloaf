#ifndef PS2_DEVICE_H
#define PS2_DEVICE_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <stdint.h>


#include "esp_rom_sys.h"   // esp_rom_delay_us
// esp_timer.h (above) supplies esp_timer_get_time()

// Unomment following line to enable debug messages on the PS2DEV module
//#define _ps2dev_DEBUG_

namespace ps2dev
{

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
  const uint32_t MOUSE_CLICK_PRESSING_DURATION_MILLIS = 100;

  class PS2Packet
  {
  public:
    uint8_t len;
    uint8_t data[16];
  };

  class PS2Device
  {
  public:
    PS2Device(gpio_num_t clk, gpio_num_t data);

    enum class BusState
    {
      IDLE,
      COMMUNICATION_INHIBITED,
      HOST_REQUEST_TO_SEND,
    };

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
    void end();
    bool running() const { return _running; }
    TaskHandle_t hostRequestTask() const { return _task_process_host_request; }
    // A task calls this on ITSELF, after releasing the bus mutex, immediately
    // before vTaskDelete(NULL).  end() waits on these going NULL.  Two
    // overloads: _task_process_host_request is volatile (read from ISR
    // context, see below), _task_send_packet is not.
    void clearTaskHandle(TaskHandle_t *slot) { *slot = nullptr; }
    void clearTaskHandle(volatile TaskHandle_t *slot) { *slot = nullptr; }
    TaskHandle_t *sendTaskSlot()                 { return &_task_send_packet; }
    volatile TaskHandle_t *hostRequestTaskSlot() { return &_task_process_host_request; }
    gpio_num_t clkPin() const  { return _ps2clk; }
    gpio_num_t dataPin() const { return _ps2data; }

  protected:
    gpio_num_t _ps2clk;
    gpio_num_t _ps2data;
    UBaseType_t _config_task_priority = DEFAULT_TASK_PRIORITY;
    BaseType_t _config_task_core = DEFAULT_TASK_CORE;
    // Read by ps2_clk_isr (ISR context) via hostRequestTask(); written by
    // task creation/teardown (task context).  volatile for the same
    // cross-context-visibility reason _running is, below.
    volatile TaskHandle_t _task_process_host_request = nullptr;
    TaskHandle_t _task_send_packet = nullptr;
    QueueHandle_t _queue_packet;
    SemaphoreHandle_t _mutex_bus;
    // Written ONLY by begin() (true, last statement) and end() (false, first
    // statement).  Nothing else touches it -- the surrounding code does
    // read-modify-write on its other state and would clobber a shared
    // sentinel.  Same discipline as IECBusHandler::m_enabled.
    volatile bool _running = false;
    void golo(gpio_num_t pin);
    void gohi(gpio_num_t pin);
    void ack();
    // Self-notify backstop for a host request whose triggering CLK edge
    // landed while write()/read() had the interrupt disabled --
    // gpio_hal_intr_enable_on_core() clears the latched pending-interrupt
    // status bit before re-arming, so that edge is discarded rather than
    // deferred.  Called only from write()/read(), right after re-enabling.
    void notifyIfHostWaiting();
  };
} // namespace ps2dev

#endif // PS2_DEVICE_H