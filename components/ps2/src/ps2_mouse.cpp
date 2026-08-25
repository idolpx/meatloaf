#include "ps2_device.h"
#include "ps2_mouse.h"
#include "esp_log.h"
#include <nvs_flash.h>   // ps2_mouse.cpp calls nvs_flash_init()
#include <string>

namespace ps2dev
{
//
// PS2 Mouse
//
void _taskfn_poll_mouse_count(void *arg)
{
  PS2Mouse *ps2mouse = (PS2Mouse *)arg;
  while (true)
  {
    xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
    if (ps2mouse->data_reporting_enabled())
    {
      ps2mouse->_report();
    }
    vTaskDelay(pdMS_TO_TICKS(1000 / ps2mouse->get_sample_rate()));
  }
  vTaskDelete(NULL);
}

void PS2Mouse::_save_internal_state_to_nvs()
{
  auto ret = nvs_set_u8(_nvs_handle, "hasWheel", _has_wheel);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::_save_internal_state_to_nvs: nvs_set_u8 failed to save hasWheel.");
  }
  ret = nvs_set_u8(_nvs_handle, "has4and5Btn", _has_4th_and_5th_buttons);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::_save_internal_state_to_nvs: nvs_set_u8 failed to save has4and5Btn.");
  }
  ret = nvs_set_u8(_nvs_handle, "dataRepEn", _data_reporting_enabled);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::_save_internal_state_to_nvs: nvs_set_u8 failed to save dataRepEn.");
  }
  ret = nvs_set_u8(_nvs_handle, "resolution", (uint8_t)_resolution);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::_save_internal_state_to_nvs: nvs_set_u8 failed to save resolution.");
  }
  ret = nvs_set_u8(_nvs_handle, "scale", (uint8_t)_scale);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::_save_internal_state_to_nvs: nvs_set_u8 failed to save scale.");
  }
  ret = nvs_set_u8(_nvs_handle, "mode", (uint8_t)_mode);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::_save_internal_state_to_nvs: nvs_set_u8 failed to save mode.");
  }
}

void PS2Mouse::_load_internal_state_from_nvs()
{
  auto ret = nvs_get_u8(_nvs_handle, "hasWheel", (uint8_t *)&_has_wheel);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::_load_internal_state_from_nvs: nvs_get_u8 failed to load hasWheel.");
  }
  nvs_get_u8(_nvs_handle, "has4and5Btn", (uint8_t *)&_has_4th_and_5th_buttons);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::_load_internal_state_from_nvs: nvs_get_u8 failed to load has4and5Btn.");
  }
  nvs_get_u8(_nvs_handle, "dataRepEn", (uint8_t *)&_data_reporting_enabled);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::_load_internal_state_from_nvs: nvs_get_u8 failed to load dataRepEn.");
  }
  nvs_get_u8(_nvs_handle, "resolution", (uint8_t *)&_resolution);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::_load_internal_state_from_nvs: nvs_get_u8 failed to load resolution.");
  }
  nvs_get_u8(_nvs_handle, "scale", (uint8_t *)&_scale);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::_load_internal_state_from_nvs: nvs_get_u8 failed to load scale.");
  }
  nvs_get_u8(_nvs_handle, "mode", (uint8_t *)&_mode);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::_load_internal_state_from_nvs: nvs_get_u8 failed to load mode.");
  }
}



PS2Mouse::PS2Mouse(gpio_num_t clk, gpio_num_t data) : PS2Device(clk, data) {}
void PS2Mouse::begin(bool restore_internal_state = 1)
{
  PS2Device::begin(DEFAULT_TASK_CORE_MOUSE);

  auto ret = nvs_flash_init();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::begin: nvs_flash_init failed");
  }
  const auto nvs_ns = std::string("ps2dev") + std::to_string(_ps2clk) + std::to_string(_ps2data);
  ret = nvs_open(nvs_ns.c_str(), NVS_READWRITE, &_nvs_handle);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "PS2Mouse::begin: nvs_open failed");
  }

  if (!restore_internal_state)
  {
    xSemaphoreTake(_mutex_bus, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(200));
    write(0xAA);
    esp_rom_delay_us(BYTE_INTERVAL_MICROS);
    write(0x00);
    xSemaphoreGive(_mutex_bus);
  }
  else if (ret == ESP_OK)
  {
    _load_internal_state_from_nvs();
    ESP_LOGI(TAG, "Internal state for mouse loaded from NVS");
    xSemaphoreTake(_mutex_bus, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(200));
    write(0xAA);
    esp_rom_delay_us(BYTE_INTERVAL_MICROS);
    write(0x00);
    xSemaphoreGive(_mutex_bus);
  }

  xTaskCreatePinnedToCore(_taskfn_poll_mouse_count, "PS2Mouse", 4096, this, _config_task_priority - 1, &_task_poll_mouse_count, DEFAULT_TASK_CORE_MOUSE);
}
int PS2Mouse::reply_to_host(uint8_t host_cmd)
{
  uint8_t val;
  if (_mode == Mode::WRAP_MODE)
  {
    switch ((Command)host_cmd)
    {
    case Command::SET_WRAP_MODE: // set wrap mode
#if defined(_ps2dev_DEBUG_)
      printf("PS2Mouse::reply_to_host: (WRAP_MODE) Set wrap mode command received");
#endif
      ack();
      reset_counter();
      break;
    case Command::RESET_WRAP_MODE: // reset wrap mode
#if defined(_ps2dev_DEBUG_)
      printf("PS2Mouse::reply_to_host: (WRAP_MODE) Reset wrap mode command received");
#endif
      ack();
      reset_counter();
      _mode = _last_mode;
      _save_internal_state_to_nvs();
      break;
    default:
      write(host_cmd);
    }
    return 0;
  }

  switch ((Command)host_cmd)
  {
  case Command::RESET: // reset
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Reset command received");
#endif
    ack();
    // the while loop lets us wait for the host to be ready
    while (write(0xAA) != 0)
      vTaskDelay(1);
    esp_rom_delay_us(BYTE_INTERVAL_MICROS);
    while (write(0x00) != 0)
      vTaskDelay(1);
    _has_wheel = false;
    _has_4th_and_5th_buttons = false;
    _sample_rate = 100;
    _resolution = ResolutionCode::RES_4;
    _scale = Scale::ONE_ONE;
    _data_reporting_enabled = false;
    _mode = Mode::STREAM_MODE;
    _save_internal_state_to_nvs();
    reset_counter();
    break;
  case Command::RESEND: // resend
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Resend command received");
#endif
    ack();
    break;
  case Command::SET_DEFAULTS: // set defaults
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Set defaults command received");
#endif
    // enter stream mode
    ack();
    _sample_rate = 100;
    _resolution = ResolutionCode::RES_4;
    _scale = Scale::ONE_ONE;
    _data_reporting_enabled = false;
    _mode = Mode::STREAM_MODE;
    _save_internal_state_to_nvs();
    reset_counter();
    break;
  case Command::DISABLE_DATA_REPORTING: // disable data reporting
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Disable data reporting command received");
#endif
    ack();
    _data_reporting_enabled = false;
    _save_internal_state_to_nvs();
    reset_counter();
    break;
  case Command::ENABLE_DATA_REPORTING: // enable data reporting
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Enable data reporting command received");
#endif
    ack();
    _data_reporting_enabled = true;
    _save_internal_state_to_nvs();
    reset_counter();
    break;
  case Command::SET_SAMPLE_RATE: // set sample rate
    ack();
    if (read(&val) == 0)
    {
      switch (val)
      {
      case 10:
      case 20:
      case 40:
      case 60:
      case 80:
      case 100:
      case 200:
        _sample_rate = val;
        _last_sample_rate[0] = _last_sample_rate[1];
        _last_sample_rate[1] = _last_sample_rate[2];
        _last_sample_rate[2] = val;
#if defined(_ps2dev_DEBUG_)
        printf("Set sample rate command received: %x", val);
        //_ps2dev_DEBUG_.println(val);
#endif
        ack();
        break;

      default:
        break;
      }
      _save_internal_state_to_nvs();
      // _min_report_interval_us = 1000000 / sample_rate;
      reset_counter();
    }
    break;
  case Command::GET_DEVICE_ID: // get device id
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Get device id command received");
#endif
    ack();
    if (_last_sample_rate[0] == 200 && _last_sample_rate[1] == 100 && _last_sample_rate[2] == 80)
    {
      write(0x03); // Intellimouse with wheel
#if defined(_ps2dev_DEBUG_)
      printf("PS2Mouse::reply_to_host: Act as Intellimouse with wheel.");
#endif
      _has_wheel = true;
      _save_internal_state_to_nvs();
    }
    else if (_last_sample_rate[0] == 200 && _last_sample_rate[1] == 200 && _last_sample_rate[2] == 80 && _has_wheel == true)
    {
      write(0x04); // Intellimouse with 4th and 5th buttons
#if defined(_ps2dev_DEBUG_)
      printf("PS2Mouse::reply_to_host: Act as Intellimouse with 4th and 5th buttons.");
#endif
      _has_4th_and_5th_buttons = true;
      _save_internal_state_to_nvs();
    }
    else
    {
      write(0x00); // Standard PS/2 mouse
#if defined(_ps2dev_DEBUG_)
      printf("PS2Mouse::reply_to_host: Act as standard PS/2 mouse.");
#endif
      _has_wheel = false;
      _has_4th_and_5th_buttons = false;
      _save_internal_state_to_nvs();
    }
    reset_counter();
    break;
  case Command::SET_REMOTE_MODE: // set remote mode
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Set remote mode command received");
#endif
    // ack();
    esp_rom_delay_us(BYTE_INTERVAL_MICROS);
    while (write(0xFA) != 0)
      vTaskDelay(1);
    esp_rom_delay_us(BYTE_INTERVAL_MICROS);
    reset_counter();
    _mode = Mode::REMOTE_MODE;
    _save_internal_state_to_nvs();
    break;
  case Command::SET_WRAP_MODE: // set wrap mode
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Set wrap mode command received");
#endif
    ack();
    reset_counter();
    _last_mode = _mode;
    _mode = Mode::WRAP_MODE;
    _save_internal_state_to_nvs();
    break;
  case Command::RESET_WRAP_MODE: // reset wrap mode
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Reset wrap mode command received");
#endif
    ack();
    reset_counter();
    break;
  case Command::READ_DATA: // read data
#if defined(_ps2dev_DEBUG_)
                           // printf("PS2Mouse::reply_to_host: Read data command received"); //////////////////////////////////////////////////////////////
#endif
    ack();
    _report();
    reset_counter();
    break;
  case Command::SET_STREAM_MODE: // set stream mode
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Set stream mode command received");
#endif
    ack();
    reset_counter();
    break;
  case Command::STATUS_REQUEST: // status request
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Status request command received");
#endif
    ack();
    _send_status();
    break;
  case Command::SET_RESOLUTION: // set resolution
    ack();
    if (read(&val) == 0 && val <= 3)
    {
      _resolution = (ResolutionCode)val;
#if defined(_ps2dev_DEBUG_)
      printf("PS2Mouse::reply_to_host: Set resolution command received: %x", val);
      //_ps2dev_DEBUG_.println(val, HEX);
#endif
      ack();
      _save_internal_state_to_nvs();
      reset_counter();
    }
    break;
  case Command::SET_SCALING_2_1: // set scaling 2:1
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Set scaling 2:1 command received");
#endif
    ack();
    _scale = Scale::TWO_ONE;
    _save_internal_state_to_nvs();
    break;
  case Command::SET_SCALING_1_1: // set scaling 1:1
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Set scaling 1:1 command received");
#endif
    ack();
    _scale = Scale::ONE_ONE;
    _save_internal_state_to_nvs();
    break;
  default:
    esp_rom_delay_us(BYTE_INTERVAL_MICROS);
    while ((write(0xFE) != 0))
      vTaskDelay(1);
#if defined(_ps2dev_DEBUG_)
    printf("PS2Mouse::reply_to_host: Unknown command receivedASD: %x", host_cmd);
    //_ps2dev_DEBUG_.println(host_cmd, HEX);
#endif
    break;
  }
  return 0;
}
bool PS2Mouse::has_wheel() { return _has_wheel; }
bool PS2Mouse::has_4th_and_5th_buttons() { return _has_4th_and_5th_buttons; }
bool PS2Mouse::data_reporting_enabled() { return _data_reporting_enabled; }
void PS2Mouse::reset_counter()
{
  _count_x = 0;
  _count_y = 0;
  _count_z = 0;
  _count_x_overflow = 0;
  _count_y_overflow = 0;
}
uint8_t PS2Mouse::get_sample_rate() { return _sample_rate; }
void PS2Mouse::move(int16_t x, int16_t y, int8_t wheel)
{
  _count_x += x;
  _count_y -= y; // We need to decrement, because USB HID inverts the vertical axis
  _count_z -= wheel;
  xTaskNotifyGive(_task_poll_mouse_count);
}
void PS2Mouse::press(Button button)
{
  switch (button)
  {
  case Button::LEFT:
    _button_left = 1;
    break;
  case Button::RIGHT:
    _button_right = 1;
    break;
  case Button::MIDDLE:
    _button_middle = 1;
    break;
  case Button::BUTTON_4:
    _button_4th = 1;
    break;
  case Button::BUTTON_5:
    _button_5th = 1;
    break;
  default:
    break;
  }
  xTaskNotifyGive(_task_poll_mouse_count);
}
void PS2Mouse::release(Button button)
{
  switch (button)
  {
  case Button::LEFT:
    _button_left = 0;
    break;
  case Button::RIGHT:
    _button_right = 0;
    break;
  case Button::MIDDLE:
    _button_middle = 0;
    break;
  case Button::BUTTON_4:
    _button_4th = 0;
    break;
  case Button::BUTTON_5:
    _button_5th = 0;
    break;
  default:
    break;
  }
  xTaskNotifyGive(_task_poll_mouse_count);
}
void PS2Mouse::click(Button button)
{
  press(button);
  vTaskDelay(pdMS_TO_TICKS(MOUSE_CLICK_PRESSING_DURATION_MILLIS));
  release(button);
}
void PS2Mouse::_report()
{
  PS2Packet packet;
  if (_scale == Scale::TWO_ONE)
  {
    int16_t *p[2] = {&_count_x, &_count_y};
    for (size_t i = 0; i < 2; i++)
    {
      bool positive = *p[i] >= 0;
      uint16_t abs_value = positive ? *p[i] : -*p[i];
      switch (abs_value)
      {
      case 1:
        abs_value = 1;
        break;
      case 2:
        abs_value = 1;
        break;
      case 3:
        abs_value = 3;
        break;
      case 4:
        abs_value = 6;
        break;
      case 5:
        abs_value = 9;
        break;
      default:
        abs_value *= 2;
        break;
      }
      if (!positive)
        *p[i] = -abs_value;
    }
  }
  if (_count_x > 255)
  {
    _count_x_overflow = 1;
    _count_x = 255;
  }
  else if (_count_x < -255)
  {
    _count_x_overflow = 1;
    _count_x = -255;
  }
  if (_count_y > 255)
  {
    _count_y_overflow = 1;
    _count_y = 255;
  }
  else if (_count_y < -255)
  {
    _count_y_overflow = 1;
    _count_y = -255;
  }
  if (_count_z > 7)
  {
    _count_z = 7;
  }
  else if (_count_z < -8)
  {
    _count_z = -8;
  }

  packet.len = 3 + _has_wheel;
  packet.data[0] = (_button_left) | ((_button_right) << 1) | ((_button_middle) << 2) | (1 << 3) | ((_count_x < 0) << 4) |
                   ((_count_y < 0) << 5) | (_count_x_overflow << 6) | (_count_y_overflow << 7);
  packet.data[1] = _count_x & 0xFF;
  packet.data[2] = _count_y & 0xFF;
  if (_has_wheel && !_has_4th_and_5th_buttons)
  {
    packet.data[3] = _count_z & 0xFF;
  }
  else if (_has_wheel && _has_4th_and_5th_buttons)
  {
    packet.data[3] = (_count_z & 0x0F) | ((_button_4th) << 4) | ((_button_5th) << 5);
  }

  send_packet(&packet);
  reset_counter();
}
void PS2Mouse::_send_status()
{
  PS2Packet packet;
  packet.len = 3;
  bool mode = (_mode == Mode::REMOTE_MODE);
  packet.data[0] = (_button_right & 1) & ((_button_middle & 1) << 1) & ((_button_left & 1) << 2) & ((0) << 3) &
                   (((uint8_t)_scale & 1) << 4) & ((_data_reporting_enabled & 1) << 5) & ((mode & 1) << 6) & ((0) << 7);
  packet.data[1] = (uint8_t)_resolution;
  packet.data[2] = _sample_rate;
  send_packet(&packet);
}


}