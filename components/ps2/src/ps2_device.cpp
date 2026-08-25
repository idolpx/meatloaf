#include "ps2_device.h"
#include "esp_log.h"

namespace ps2dev
{

  // A host request to send is CLK high && DATA low.  The host's sequence is
  // CLK low (inhibit) -> DATA low (start bit) -> CLK released, so the edge
  // that CREATES the condition is CLK rising.
  static void IRAM_ATTR ps2_clk_isr(void *arg)
  {
    PS2Device *dev = static_cast<PS2Device *>(arg);
    if (gpio_get_level(dev->clkPin()) == 1 && gpio_get_level(dev->dataPin()) == 0)
    {
      BaseType_t hpw = pdFALSE;
      vTaskNotifyGiveFromISR(dev->hostRequestTask(), &hpw);
      if (hpw)
        portYIELD_FROM_ISR();
    }
  }

  PS2Device::PS2Device(gpio_num_t clk, gpio_num_t data)
  {
    _ps2clk = clk;
    _ps2data = data;
  }

  void PS2Device::config(UBaseType_t task_priority, BaseType_t task_core)
  {
    if (task_priority < 1)
    {
      task_priority = 1;
    }
    else if (task_priority > configMAX_PRIORITIES)
    {
      task_priority = configMAX_PRIORITIES - 1;
    }
    _config_task_priority = task_priority;
    _config_task_core = task_core;
  }

  void PS2Device::begin(BaseType_t core)
  {
    gpio_config_t io_conf; // PIN CONFIGURATION SECTION: CRITICAL

    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT_OD;
    io_conf.pin_bit_mask = (1ULL << _ps2data);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    io_conf.pin_bit_mask = (1ULL << _ps2clk);
    gpio_config(&io_conf);

    gohi(_ps2clk);
    gohi(_ps2data);
    _mutex_bus = xSemaphoreCreateMutex();
    _queue_packet = xQueueCreate(PACKET_QUEUE_LENGTH, sizeof(PS2Packet));

    // The service is installed once, globally, at src/main.cpp:238.  Calling
    // gpio_install_isr_service() here returns ESP_ERR_INVALID_STATE and, worse,
    // is what the deleted ps2keyboard.cpp used to do.
    gpio_set_intr_type(_ps2clk, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(_ps2clk, ps2_clk_isr, this);
    gpio_intr_disable(_ps2clk);   // enabled by the task once it is running

    _running = true;
  }

  void PS2Device::end()
  {
    if (!_running)
      return;
    _running = false;                  // both loops observe this at the top

    // The host-request task blocks on a notification with no timeout, so it
    // must be woken explicitly.  The send task's bounded queue receive wakes
    // on its own within 250 ms.
    if (_task_process_host_request)
      xTaskNotifyGive(_task_process_host_request);

    for (int i = 0; i < 100 && (_task_send_packet || _task_process_host_request); i++)
      vTaskDelay(pdMS_TO_TICKS(10));   // bounded, <= 1 s

    if (_task_send_packet || _task_process_host_request)
    {
      // Freeing a mutex a live task may still take is a guaranteed crash;
      // leaking ~440 bytes is survivable and leaves this line behind.
      ESP_LOGE("ps2", "tasks did not exit; leaking mutex/queue deliberately");
      return;
    }

    gpio_isr_handler_remove(_ps2clk);
    gohi(_ps2clk);
    gohi(_ps2data);
    gpio_reset_pin(_ps2clk);
    gpio_reset_pin(_ps2data);

    vQueueDelete(_queue_packet);
    _queue_packet = nullptr;
    vSemaphoreDelete(_mutex_bus);
    _mutex_bus = nullptr;
  }

  void PS2Device::gohi(gpio_num_t pin)
  {
    gpio_set_level(pin, 1);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
  }
  void PS2Device::golo(gpio_num_t pin)
  {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 0);
  }
  void PS2Device::ack()
  {
    esp_rom_delay_us(BYTE_INTERVAL_MICROS);
    write(0xFA);
    esp_rom_delay_us(BYTE_INTERVAL_MICROS);
  }
  int PS2Device::write(unsigned char data)
  {
    unsigned char i;
    unsigned char parity = 1;

    if (get_bus_state() != BusState::IDLE)
    {
      return -1;
    }

    gpio_intr_disable(_ps2clk);

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&mux);

    golo(_ps2data);
    esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
    // device sends on falling clock
    golo(_ps2clk); // start bit
    esp_rom_delay_us(CLK_HALF_PERIOD_MICROS);
    gohi(_ps2clk);
    esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);

    for (i = 0; i < 8; i++)
    {
      if (data & 0x01)
      {
        gohi(_ps2data);
      }
      else
      {
        golo(_ps2data);
      }
      esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
      golo(_ps2clk);
      esp_rom_delay_us(CLK_HALF_PERIOD_MICROS);
      gohi(_ps2clk);
      esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);

      parity = parity ^ (data & 0x01);
      data = data >> 1;
    }
    // parity bit
    if (parity)
    {
      gohi(_ps2data);
    }
    else
    {
      golo(_ps2data);
    }
    esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
    golo(_ps2clk);
    esp_rom_delay_us(CLK_HALF_PERIOD_MICROS);
    gohi(_ps2clk);
    esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);

    // stop bit
    gohi(_ps2data);
    esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
    golo(_ps2clk);
    esp_rom_delay_us(CLK_HALF_PERIOD_MICROS);
    gohi(_ps2clk);
    esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);

    taskEXIT_CRITICAL(&mux);

    gpio_intr_enable(_ps2clk);

    return 0;
  }
  int PS2Device::write_wait_idle(uint8_t data, uint64_t timeout_micros)
  {
    int64_t start_time = esp_timer_get_time();
    while (get_bus_state() != BusState::IDLE)
    {
      if (esp_timer_get_time() - start_time > (int64_t)timeout_micros)
      {
        return -1;
      }
    }
    return write(data);
  }
  int PS2Device::read(unsigned char *value, uint64_t timeout_ms)
  {
    unsigned int data = 0x00;
    unsigned int bit = 0x01;

    unsigned char calculated_parity = 1;
    unsigned char received_parity = 0;

    // wait for data line to go low and clock line to go high (or timeout)
    int64_t waiting_since = esp_timer_get_time();
    while (get_bus_state() != BusState::HOST_REQUEST_TO_SEND)
    {
      if ((esp_timer_get_time() - waiting_since) > (int64_t)timeout_ms * 1000)
        return -1;
      esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
    }

    gpio_intr_disable(_ps2clk);

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&mux);

    esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
    golo(_ps2clk);
    esp_rom_delay_us(CLK_HALF_PERIOD_MICROS);
    gohi(_ps2clk);
    esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);

    while (bit < 0x0100)
    {
      if (gpio_get_level(_ps2data) == 1)
      {
        data = data | bit;
        calculated_parity = calculated_parity ^ 1;
      }
      else
      {
        calculated_parity = calculated_parity ^ 0;
      }

      bit = bit << 1;

      esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
      golo(_ps2clk);
      esp_rom_delay_us(CLK_HALF_PERIOD_MICROS);
      gohi(_ps2clk);
      esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
    }
    // we do the delay at the end of the loop, so at this point we have
    // already done the delay for the parity bit

    // parity bit
    if (gpio_get_level(_ps2data) == 1)
    {
      received_parity = 1;
    }

    // stop bit
    esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
    golo(_ps2clk);
    esp_rom_delay_us(CLK_HALF_PERIOD_MICROS);
    gohi(_ps2clk);
    esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);

    esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
    golo(_ps2data);
    golo(_ps2clk);
    esp_rom_delay_us(CLK_HALF_PERIOD_MICROS);
    gohi(_ps2clk);
    esp_rom_delay_us(CLK_QUATER_PERIOD_MICROS);
    gohi(_ps2data);

    taskEXIT_CRITICAL(&mux);

    gpio_intr_enable(_ps2clk);

    *value = data & 0x00FF;

    if (received_parity == calculated_parity)
    {
      return 0;
    }
    else
    {
      return -2;
    }
  }
  PS2Device::BusState PS2Device::get_bus_state()
  {
    if (gpio_get_level(_ps2clk) == 0)
    {
      return BusState::COMMUNICATION_INHIBITED;
    }
    else if (gpio_get_level(_ps2data) == 0)
    {
      return BusState::HOST_REQUEST_TO_SEND;
    }
    else
    {
      return BusState::IDLE;
    }
  }
  SemaphoreHandle_t PS2Device::get_bus_mutex_handle() { return _mutex_bus; }
  QueueHandle_t PS2Device::get_packet_queue_handle() { return _queue_packet; }
  int PS2Device::send_packet(PS2Packet *packet)
  {
    // A send racing PS2Device::end() must fail, not write to a deleted
    // handle -- end() sets _queue_packet = nullptr after vQueueDelete().
    // This guard is load-bearing; it is not about backpressure.
    if (!_queue_packet)
      return -1;
    // The timeout is a stuck-wire backstop, not flow control: type()'s own
    // vTaskDelay(pdMS_TO_TICKS(10)) between keydown and keyup already keeps
    // the producer well under the consumer's drain rate, so ordinary typing
    // never comes close to filling a 20-deep queue.  A 0 ms timeout would
    // misreport transient scheduling jitter as failure; 500 ms lets it
    // instead catch a genuinely wedged bus (host never toggling clock).
    return (xQueueSend(_queue_packet, packet, pdMS_TO_TICKS(500)) == pdTRUE) ? 0 : -1;
  }
}