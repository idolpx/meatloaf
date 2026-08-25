#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H

#include "ps2_device.h"

#include <initializer_list>
#include <stack>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdint.h>
#include "scan_codes_set_2.h"
#include <nvs_flash.h>
#include <string>


namespace ps2dev
{
  class PS2Keyboard : public PS2Device
  {
  public:
    PS2Keyboard(gpio_num_t clk, gpio_num_t data);
    int reply_to_host(uint8_t host_cmd);
    enum class Command
    {
      RESET = 0xFF,
      RESEND = 0xFE,
      ACK = 0xFA,
      SET_DEFAULTS = 0xF6,
      DISABLE_DATA_REPORTING = 0xF5,
      ENABLE_DATA_REPORTING = 0xF4,
      SET_TYPEMATIC_RATE = 0xF3,
      GET_DEVICE_ID = 0xF2,
      SET_SCAN_CODE_SET = 0xF0,
      ECHO = 0xEE,
      SET_RESET_LEDS = 0xED,
      BAT_SUCCESS = 0xAA,
    };
    void begin();
    bool data_reporting_enabled();
    bool is_scroll_lock_led_on();
    bool is_num_lock_led_on();
    bool is_caps_lock_led_on();
    void keydown(scancodes::Key key);
    void keyup(scancodes::Key key);
    void type(scancodes::Key key);
    void type(std::initializer_list<scancodes::Key> keys);
    void type(const char *str);
    void keyHid_send(uint8_t btkey, bool keyDown);
    void keyHid_send_CCONTROL(uint16_t btkey, bool keyDown);

  protected:
    bool _data_reporting_enabled = true;
    bool _led_scroll_lock = false;
    bool _led_num_lock = false;
    bool _led_caps_lock = false;
  };

  void _taskfn_process_host_request(void *arg);
  void _taskfn_send_packet(void *arg);
} // namespace ps2dev

#endif // __ps2dev_H__