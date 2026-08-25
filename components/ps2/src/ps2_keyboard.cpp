#include "ps2_device.h"
#include "ps2_keyboard.h"
#include "esp_log.h"
#include <stack>

namespace ps2dev
{
//
// PS2 Keyboard
//


void _taskfn_process_host_request(void *arg)
{
  PS2Device *ps2dev = (PS2Device *)arg;
  while (true)
  {
    xSemaphoreTake(ps2dev->get_bus_mutex_handle(), portMAX_DELAY);
    if (ps2dev->get_bus_state() == PS2Device::BusState::HOST_REQUEST_TO_SEND)
    {
      uint8_t host_cmd;
      if (ps2dev->read(&host_cmd) == 0)
      {
        ps2dev->reply_to_host(host_cmd);
      }
    }
    xSemaphoreGive(ps2dev->get_bus_mutex_handle());
    vTaskDelay(pdMS_TO_TICKS(INTERVAL_CHECKING_HOST_SEND_REQUEST_MILLIS));
  }
  vTaskDelete(NULL);
}
void _taskfn_send_packet(void *arg)
{
  PS2Device *ps2dev = (PS2Device *)arg;
  while (true)
  {
    PS2Packet packet;
    //if (xQueueReceive(ps2dev->get_packet_queue_handle(), &packet, portMAX_DELAY) == pdTRUE)
    if (xQueueReceive(ps2dev->get_packet_queue_handle(), &packet, 0) == pdTRUE)
    {
      xSemaphoreTake(ps2dev->get_bus_mutex_handle(), portMAX_DELAY);
      esp_rom_delay_us(BYTE_INTERVAL_MICROS);
      for (int i = 0; i < packet.len; i++)
      {
        ps2dev->write_wait_idle(packet.data[i]);
        esp_rom_delay_us(BYTE_INTERVAL_MICROS);
      }
      xSemaphoreGive(ps2dev->get_bus_mutex_handle());
    }
    portYIELD();
  }
  vTaskDelete(NULL);
}

PS2Keyboard::PS2Keyboard(gpio_num_t clk, gpio_num_t data) : PS2Device(clk, data) {

}

void PS2Keyboard::begin()
{
  PS2Device::begin();

  xTaskCreatePinnedToCore(_taskfn_process_host_request, "process_host_request", 4096, this, _config_task_priority, &_task_process_host_request, DEFAULT_TASK_CORE);
  xTaskCreatePinnedToCore(_taskfn_send_packet, "send_packet", 4096, this, _config_task_priority - 1, &_task_send_packet, DEFAULT_TASK_CORE);

  xSemaphoreTake(_mutex_bus, portMAX_DELAY);
  esp_rom_delay_us(BYTE_INTERVAL_MICROS);
  vTaskDelay(pdMS_TO_TICKS(200));
  write(0xAA);
  xSemaphoreGive(_mutex_bus);
}
bool PS2Keyboard::data_reporting_enabled() { return _data_reporting_enabled; }
bool PS2Keyboard::is_scroll_lock_led_on() { return _led_scroll_lock; }
bool PS2Keyboard::is_num_lock_led_on() { return _led_num_lock; }
bool PS2Keyboard::is_caps_lock_led_on() { return _led_caps_lock; }
int PS2Keyboard::reply_to_host(uint8_t host_cmd)
{
  uint8_t val;
  switch ((Command)host_cmd)
  {
  case Command::RESET: // reset
#if defined(_ps2dev_DEBUG_)
    printf("PS2Keyboard::reply_to_host: Reset command received");
#endif // _ps2dev_DEBUG_
     // the while loop lets us wait for the host to be ready
    _data_reporting_enabled = false;
    ack(); // ack() provides delay, some systems need it
    while (write((uint8_t)Command::BAT_SUCCESS) != 0)
      vTaskDelay(1);
    _data_reporting_enabled = true; // some systems don't enable data reporting after issuing a RESET command, so we do it by default
    break;
  case Command::RESEND: // resend
#if defined(_ps2dev_DEBUG_)
    printf("PS2Keyboard::reply_to_host: Resend command received");
#endif // _ps2dev_DEBUG_
    ack();
    break;
  case Command::SET_DEFAULTS: // set defaults
#if defined(_ps2dev_DEBUG_)
    printf("PS2Keyboard::reply_to_host: Set defaults command received");
#endif // _ps2dev_DEBUG_
     // enter stream mode
    ack();
    break;
  case Command::DISABLE_DATA_REPORTING: // disable data reporting
#if defined(_ps2dev_DEBUG_)
    printf("PS2Keyboard::reply_to_host: Disable data reporting command received");
#endif // _ps2dev_DEBUG_
    _data_reporting_enabled = false;
    ack();
    break;
  case Command::ENABLE_DATA_REPORTING: // enable data reporting
#if defined(_ps2dev_DEBUG_)
    printf("PS2Keyboard::reply_to_host: Enable data reporting command received");
#endif // _ps2dev_DEBUG_
    _data_reporting_enabled = true;
    ack();
    break;
  case Command::SET_TYPEMATIC_RATE: // set typematic rate
#if defined(_ps2dev_DEBUG_)
    printf("PS2Keyboard::reply_to_host: Set typematic rate command received");
#endif // _ps2dev_DEBUG_
    ack();
    if (!read(&val))
      ack(); // do nothing with the rate
    break;
  case Command::GET_DEVICE_ID: // get device id
#if defined(_ps2dev_DEBUG_)
    printf("PS2Keyboard::reply_to_host: Get device id command received");
#endif // _ps2dev_DEBUG_
    ack();
    while (write(0xAB) != 0)
      vTaskDelay(1); // ensure ID gets writed, some hosts may be sensitive
    while (write(0x83) != 0)
      vTaskDelay(1); // this is critical for combined ports (they decide mouse/kb on this)
    break;
  case Command::SET_SCAN_CODE_SET: // set scan code set
#if defined(_ps2dev_DEBUG_)
    printf("PS2Keyboard::reply_to_host: Set scan code set command received");
#endif // _ps2dev_DEBUG_
    ack();
    if (!read(&val))
      ack(); // do nothing with the rate
    break;
  case Command::ECHO: // echo
#if defined(_ps2dev_DEBUG_)
    printf("PS2Keyboard::reply_to_host: Echo command received");
#endif // _ps2dev_DEBUG_
    esp_rom_delay_us(BYTE_INTERVAL_MICROS);
    write(0xEE);
    esp_rom_delay_us(BYTE_INTERVAL_MICROS);
    break;
  case Command::SET_RESET_LEDS: // set/reset LEDs
#if defined(_ps2dev_DEBUG_)
    printf("PS2Keyboard::reply_to_host: Set/reset LEDs command received");
#endif // _ps2dev_DEBUG_
    esp_rom_delay_us(BYTE_INTERVAL_MICROS);
    while (write(0xFA) != 0)
      vTaskDelay(1);
    esp_rom_delay_us(BYTE_INTERVAL_MICROS);
    if (!read(&val, 10))
    {
      esp_rom_delay_us(BYTE_INTERVAL_MICROS);
      while (write(0xFA) != 0)
        vTaskDelay(1);
      esp_rom_delay_us(BYTE_INTERVAL_MICROS);
      _led_scroll_lock = ((val & 1) != 0);
      _led_num_lock = ((val & 2) != 0);
      _led_caps_lock = ((val & 4) != 0);
    }
    return 1;
    break;
  default:
    ack();
#if defined(_ps2dev_DEBUG_)
    printf("PS2Keyboard::reply_to_host: Unknown command received: %x", host_cmd);
    //_ps2dev_DEBUG_.println(host_cmd, HEX);
#endif // _ps2dev_DEBUG_
    break;
  }

  return 0;
}
void PS2Keyboard::keydown(scancodes::Key key)
{
  if (!_data_reporting_enabled)
    return;
  PS2Packet packet;
  packet.len = scancodes::MAKE_CODES_LEN[key];
  for (uint8_t i = 0; i < packet.len; i++)
  {
    packet.data[i] = scancodes::MAKE_CODES[key][i];
  }
  send_packet(&packet);
}
void PS2Keyboard::keyup(scancodes::Key key)
{
  if (!_data_reporting_enabled)
    return;
  PS2Packet packet;
  packet.len = scancodes::BREAK_CODES_LEN[key];
  for (uint8_t i = 0; i < packet.len; i++)
  {
    packet.data[i] = scancodes::BREAK_CODES[key][i];
  }
  send_packet(&packet);
}
void PS2Keyboard::type(scancodes::Key key)
{
  keydown(key);
  vTaskDelay(pdMS_TO_TICKS(10));
  keyup(key);
}
void PS2Keyboard::type(std::initializer_list<scancodes::Key> keys)
{
  std::stack<scancodes::Key> stack;
  for (auto key : keys)
  {
    keydown(key);
    stack.push(key);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  while (!stack.empty())
  {
    keyup(stack.top());
    stack.pop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void PS2Keyboard::type(const char *str)
{
  size_t i = 0;
  while (str[i] != '\0')
  {
    char c = str[i];
    scancodes::Key key;
    bool shift = false;
    switch (c)
    {
    case '\b':
      key = scancodes::Key::K_BACKSPACE;
      break;
    case '\t':
      key = scancodes::Key::K_TAB;
      break;
    case '\r':
    case '\n':
      key = scancodes::Key::K_RETURN;
      break;
    case ' ':
      key = scancodes::Key::K_SPACE;
      break;
    case '!':
      shift = true;
      key = scancodes::Key::K_1;
      break;
    case '\"':
      shift = true;
      key = scancodes::Key::K_QUOTE;
      break;
    case '#':
      shift = true;
      key = scancodes::Key::K_3;
      break;
    case '$':
      shift = true;
      key = scancodes::Key::K_4;
      break;
    case '&':
      shift = true;
      key = scancodes::Key::K_7;
      break;
    case '\'':
      key = scancodes::Key::K_QUOTE;
      break;
    case '(':
      shift = true;
      key = scancodes::Key::K_9;
      break;
    case ')':
      shift = true;
      key = scancodes::Key::K_0;
      break;
    case '*':
      shift = true;
      key = scancodes::Key::K_8;
      break;
    case '+':
      shift = true;
      key = scancodes::Key::K_EQUALS;
      break;
    case ',':
      key = scancodes::Key::K_COMMA;
      break;
    case '-':
      key = scancodes::Key::K_MINUS;
      break;
    case '.':
      key = scancodes::Key::K_PERIOD;
      break;
    case '/':
      key = scancodes::Key::K_SLASH;
      break;
    case '0':
      key = scancodes::Key::K_0;
      break;
    case '1':
      key = scancodes::Key::K_1;
      break;
    case '2':
      key = scancodes::Key::K_2;
      break;
    case '3':
      key = scancodes::Key::K_3;
      break;
    case '4':
      key = scancodes::Key::K_4;
      break;
    case '5':
      key = scancodes::Key::K_5;
      break;
    case '6':
      key = scancodes::Key::K_6;
      break;
    case '7':
      key = scancodes::Key::K_7;
      break;
    case '8':
      key = scancodes::Key::K_8;
      break;
    case '9':
      key = scancodes::Key::K_9;
      break;
    case ':':
      shift = true;
      key = scancodes::Key::K_SEMICOLON;
      break;
    case ';':
      key = scancodes::Key::K_SEMICOLON;
      break;
    case '<':
      shift = true;
      key = scancodes::Key::K_COMMA;
      break;
    case '=':
      key = scancodes::Key::K_EQUALS;
      break;
    case '>':
      shift = true;
      key = scancodes::Key::K_PERIOD;
      break;
    case '\?':
      shift = true;
      key = scancodes::Key::K_SLASH;
      break;
    case '@':
      shift = true;
      key = scancodes::Key::K_2;
      break;
    case '[':
      key = scancodes::Key::K_LEFTBRACKET;
      break;
    case '\\':
      key = scancodes::Key::K_BACKSLASH;
      break;
    case ']':
      key = scancodes::Key::K_RIGHTBRACKET;
      break;
    case '^':
      shift = true;
      key = scancodes::Key::K_6;
      break;
    case '_':
      shift = true;
      key = scancodes::Key::K_MINUS;
      break;
    case '`':
      key = scancodes::Key::K_BACKQUOTE;
      break;
    case 'a':
      key = scancodes::Key::K_A;
      break;
    case 'b':
      key = scancodes::Key::K_B;
      break;
    case 'c':
      key = scancodes::Key::K_C;
      break;
    case 'd':
      key = scancodes::Key::K_D;
      break;
    case 'e':
      key = scancodes::Key::K_E;
      break;
    case 'f':
      key = scancodes::Key::K_F;
      break;
    case 'g':
      key = scancodes::Key::K_G;
      break;
    case 'h':
      key = scancodes::Key::K_H;
      break;
    case 'i':
      key = scancodes::Key::K_I;
      break;
    case 'j':
      key = scancodes::Key::K_J;
      break;
    case 'k':
      key = scancodes::Key::K_K;
      break;
    case 'l':
      key = scancodes::Key::K_L;
      break;
    case 'm':
      key = scancodes::Key::K_M;
      break;
    case 'n':
      key = scancodes::Key::K_N;
      break;
    case 'o':
      key = scancodes::Key::K_O;
      break;
    case 'p':
      key = scancodes::Key::K_P;
      break;
    case 'q':
      key = scancodes::Key::K_Q;
      break;
    case 'r':
      key = scancodes::Key::K_R;
      break;
    case 's':
      key = scancodes::Key::K_S;
      break;
    case 't':
      key = scancodes::Key::K_T;
      break;
    case 'u':
      key = scancodes::Key::K_U;
      break;
    case 'v':
      key = scancodes::Key::K_V;
      break;
    case 'w':
      key = scancodes::Key::K_W;
      break;
    case 'x':
      key = scancodes::Key::K_X;
      break;
    case 'y':
      key = scancodes::Key::K_Y;
      break;
    case 'z':
      key = scancodes::Key::K_Z;
      break;
    case 'A':
      shift = true;
      key = scancodes::Key::K_A;
      break;
    case 'B':
      shift = true;
      key = scancodes::Key::K_B;
      break;
    case 'C':
      shift = true;
      key = scancodes::Key::K_C;
      break;
    case 'D':
      shift = true;
      key = scancodes::Key::K_D;
      break;
    case 'E':
      shift = true;
      key = scancodes::Key::K_E;
      break;
    case 'F':
      shift = true;
      key = scancodes::Key::K_F;
      break;
    case 'G':
      shift = true;
      key = scancodes::Key::K_G;
      break;
    case 'H':
      shift = true;
      key = scancodes::Key::K_H;
      break;
    case 'I':
      shift = true;
      key = scancodes::Key::K_I;
      break;
    case 'J':
      shift = true;
      key = scancodes::Key::K_J;
      break;
    case 'K':
      shift = true;
      key = scancodes::Key::K_K;
      break;
    case 'L':
      shift = true;
      key = scancodes::Key::K_L;
      break;
    case 'M':
      shift = true;
      key = scancodes::Key::K_M;
      break;
    case 'N':
      shift = true;
      key = scancodes::Key::K_N;
      break;
    case 'O':
      shift = true;
      key = scancodes::Key::K_O;
      break;
    case 'P':
      shift = true;
      key = scancodes::Key::K_P;
      break;
    case 'Q':
      shift = true;
      key = scancodes::Key::K_Q;
      break;
    case 'R':
      shift = true;
      key = scancodes::Key::K_R;
      break;
    case 'S':
      shift = true;
      key = scancodes::Key::K_S;
      break;
    case 'T':
      shift = true;
      key = scancodes::Key::K_T;
      break;
    case 'U':
      shift = true;
      key = scancodes::Key::K_U;
      break;
    case 'V':
      shift = true;
      key = scancodes::Key::K_V;
      break;
    case 'W':
      shift = true;
      key = scancodes::Key::K_W;
      break;
    case 'X':
      shift = true;
      key = scancodes::Key::K_X;
      break;
    case 'Y':
      shift = true;
      key = scancodes::Key::K_Y;
      break;
    case 'Z':
      shift = true;
      key = scancodes::Key::K_Z;
      break;

    default:
      i++;
      continue;
      break;
    }
    if (shift)
    {
      keydown(scancodes::Key::K_LSHIFT);
      vTaskDelay(pdMS_TO_TICKS(10));
      type(key);
      vTaskDelay(pdMS_TO_TICKS(10));
      keyup(scancodes::Key::K_LSHIFT);
    }
    else
    {
      type(key);
    }
    i++;
  }
}

void PS2Keyboard::keyHid_send(uint8_t btkey, bool keyDown)
{
  scancodes::Key key;
  switch (btkey)
  {
  case 0x04:
    key = scancodes::Key::K_A;
    break;
  case 0x05:
    key = scancodes::Key::K_B;
    break;
  case 0x06:
    key = scancodes::Key::K_C;
    break;
  case 0x07:
    key = scancodes::Key::K_D;
    break;
  case 0x08:
    key = scancodes::Key::K_E;
    break;
  case 0x09:
    key = scancodes::Key::K_F;
    break;
  case 0x0A:
    key = scancodes::Key::K_G;
    break;
  case 0x0B:
    key = scancodes::Key::K_H;
    break;
  case 0x0C:
    key = scancodes::Key::K_I;
    break;
  case 0x0D:
    key = scancodes::Key::K_J;
    break;
  case 0x0E:
    key = scancodes::Key::K_K;
    break;
  case 0x0F:
    key = scancodes::Key::K_L;
    break;
  case 0x10:
    key = scancodes::Key::K_M;
    break;
  case 0x11:
    key = scancodes::Key::K_N;
    break;
  case 0x12:
    key = scancodes::Key::K_O;
    break;
  case 0x13:
    key = scancodes::Key::K_P;
    break;
  case 0x14:
    key = scancodes::Key::K_Q;
    break;
  case 0x15:
    key = scancodes::Key::K_R;
    break;
  case 0x16:
    key = scancodes::Key::K_S;
    break;
  case 0x17:
    key = scancodes::Key::K_T;
    break;
  case 0x18:
    key = scancodes::Key::K_U;
    break;
  case 0x19:
    key = scancodes::Key::K_V;
    break;
  case 0x1A:
    key = scancodes::Key::K_W;
    break;
  case 0x1B:
    key = scancodes::Key::K_X;
    break;
  case 0x1C:
    key = scancodes::Key::K_Y;
    break;
  case 0x1D:
    key = scancodes::Key::K_Z;
    break;
  case 0x1E:
    key = scancodes::Key::K_1;
    break;
  case 0x1F:
    key = scancodes::Key::K_2;
    break;
  case 0x20:
    key = scancodes::Key::K_3;
    break;
  case 0x21:
    key = scancodes::Key::K_4;
    break;
  case 0x22:
    key = scancodes::Key::K_5;
    break;
  case 0x23:
    key = scancodes::Key::K_6;
    break;
  case 0x24:
    key = scancodes::Key::K_7;
    break;
  case 0x25:
    key = scancodes::Key::K_8;
    break;
  case 0x26:
    key = scancodes::Key::K_9;
    break;
  case 0x27:
    key = scancodes::Key::K_0;
    break;
  case 0x28:
    key = scancodes::Key::K_RETURN;
    break;
  case 0x29:
    key = scancodes::Key::K_ESCAPE;
    break;
  case 0x2A:
    key = scancodes::Key::K_BACKSPACE;
    break;
  case 0x2B:
    key = scancodes::Key::K_TAB;
    break;
  case 0x2C:
    key = scancodes::Key::K_SPACE;
    break;
  case 0x2D:
    key = scancodes::Key::K_MINUS;
    break;
  case 0x2E:
    key = scancodes::Key::K_EQUALS;
    break;
  case 0x2F:
    key = scancodes::Key::K_LEFTBRACKET;
    break;
  case 0x30:
    key = scancodes::Key::K_RIGHTBRACKET;
    break;
  case 0x31:
    key = scancodes::Key::K_BACKSLASH;
    break;
  case 0x33:
    key = scancodes::Key::K_SEMICOLON;
    break;
  case 0x34:
    key = scancodes::Key::K_QUOTE;
    break;
  case 0x35:
    key = scancodes::Key::K_BACKQUOTE;
    break;
  case 0x36:
    key = scancodes::Key::K_COMMA;
    break;
  case 0x37:
    key = scancodes::Key::K_PERIOD;
    break;
  case 0x38:
    key = scancodes::Key::K_SLASH;
    break;
  case 0x39:
    key = scancodes::Key::K_CAPSLOCK;
    break;
  case 0x3A:
    key = scancodes::Key::K_F1;
    break;
  case 0x3B:
    key = scancodes::Key::K_F2;
    break;
  case 0x3C:
    key = scancodes::Key::K_F3;
    break;
  case 0x3D:
    key = scancodes::Key::K_F4;
    break;
  case 0x3E:
    key = scancodes::Key::K_F5;
    break;
  case 0x3F:
    key = scancodes::Key::K_F6;
    break;
  case 0x40:
    key = scancodes::Key::K_F7;
    break;
  case 0x41:
    key = scancodes::Key::K_F8;
    break;
  case 0x42:
    key = scancodes::Key::K_F9;
    break;
  case 0x43:
    key = scancodes::Key::K_F10;
    break;
  case 0x44:
    key = scancodes::Key::K_F11;
    break;
  case 0x45:
    key = scancodes::Key::K_F12;
    break;
  case 0x46:
    key = scancodes::Key::K_PRINT;
    break;
  case 0x47:
    key = scancodes::Key::K_SCROLLOCK;
    break;
  case 0x48:
    key = scancodes::Key::K_PAUSE;
    break;
  case 0x49:
    key = scancodes::Key::K_INSERT;
    break;
  case 0x4A:
    key = scancodes::Key::K_HOME;
    break;
  case 0x4B:
    key = scancodes::Key::K_PAGEUP;
    break;
  case 0x4C:
    key = scancodes::Key::K_DELETE;
    break;
  case 0x4D:
    key = scancodes::Key::K_END;
    break;
  case 0x4E:
    key = scancodes::Key::K_PAGEDOWN;
    break;
  case 0x4F:
    key = scancodes::Key::K_RIGHT;
    break;
  case 0x50:
    key = scancodes::Key::K_LEFT;
    break;
  case 0x51:
    key = scancodes::Key::K_DOWN;
    break;
  case 0x52:
    key = scancodes::Key::K_UP;
    break;
  case 0x53:
    key = scancodes::Key::K_NUMLOCK;
    break;
  case 0x54:
    key = scancodes::Key::K_KP_DIVIDE;
    break;
  case 0x55:
    key = scancodes::Key::K_KP_MULTIPLY;
    break;
  case 0x56:
    key = scancodes::Key::K_KP_MINUS;
    break;
  case 0x57:
    key = scancodes::Key::K_KP_PLUS;
    break;
  case 0x58:
    key = scancodes::Key::K_KP_ENTER;
    break;
  case 0x59:
    key = scancodes::Key::K_KP1;
    break;
  case 0x5A:
    key = scancodes::Key::K_KP2;
    break;
  case 0x5B:
    key = scancodes::Key::K_KP3;
    break;
  case 0x5C:
    key = scancodes::Key::K_KP4;
    break;
  case 0x5D:
    key = scancodes::Key::K_KP5;
    break;
  case 0x5E:
    key = scancodes::Key::K_KP6;
    break;
  case 0x5F:
    key = scancodes::Key::K_KP7;
    break;
  case 0x60:
    key = scancodes::Key::K_KP8;
    break;
  case 0x61:
    key = scancodes::Key::K_KP9;
    break;
  case 0x62:
    key = scancodes::Key::K_KP0;
    break;
  case 0x63:
    key = scancodes::Key::K_KP_PERIOD;
    break;
  case 0x65:
    key = scancodes::Key::K_MENU;
    break;
  case 0x66:
    key = scancodes::Key::K_ACPI_POWER;
    break;
  case 0x74:
    key = scancodes::Key::K_MEDIA_PLAY_PAUSE;
    break;
  case 0x78:
    key = scancodes::Key::K_MEDIA_STOP;
    break;
  case 0x7F:
    key = scancodes::Key::K_MEDIA_MUTE;
    break;
  case 0x80:
    key = scancodes::Key::K_MEDIA_VOLUME_UP;
    break;
  case 0x81:
    key = scancodes::Key::K_MEDIA_VOLUME_DOWN;
    break;
  case 0xE0:
    key = scancodes::Key::K_LCTRL;
    break;
  case 0xE1:
    key = scancodes::Key::K_LSHIFT;
    break;
  case 0xE2:
    key = scancodes::Key::K_LALT;
    break;
  case 0xE3:
    key = scancodes::Key::K_LSUPER;
    break;
  case 0xE4:
    key = scancodes::Key::K_RCTRL;
    break;
  case 0xE5:
    key = scancodes::Key::K_RSHIFT;
    break;
  case 0xE6:
    key = scancodes::Key::K_RALT;
    break;
  case 0xE7:
    key = scancodes::Key::K_RSUPER;
    break;

  default:
    return;
    break;
  }

  if (keyDown)
    keydown(key);
  else
    keyup(key);
}

void PS2Keyboard::keyHid_send_CCONTROL(uint16_t btkey, bool keyDown)
{
  scancodes::Key key;
  switch (btkey)
  {
  case 0xCD:
    key = scancodes::Key::K_MEDIA_PLAY_PAUSE;
    break;
  case 0xE9:
    key = scancodes::Key::K_MEDIA_VOLUME_UP;
    break;
  case 0xEA:
    key = scancodes::Key::K_MEDIA_VOLUME_DOWN;
    break;
  case 0xB6:
    key = scancodes::Key::K_MEDIA_PREV_TRACK;
    break;
  case 0xB5:
    key = scancodes::Key::K_MEDIA_NEXT_TRACK;
    break;
  case 0x183:
    key = scancodes::Key::K_MEDIA_MEDIA_SELECT;
    break;
  case 0x18A:
    key = scancodes::Key::K_MEDIA_EMAIL;
    break;
  case 0xE2:
    key = scancodes::Key::K_MEDIA_MUTE;
    break;
  case 0x221:
    key = scancodes::Key::K_MEDIA_WWW_SEARCH;
    break;
  case 0x223:
    key = scancodes::Key::K_HOME;
    break;
  case 0x196:
    key = scancodes::Key::K_MEDIA_WWW_HOME;
    break;
  case 0x224:
    key = scancodes::Key::K_MEDIA_WWW_BACK;
    break;

  default:
    return;
    break;
  }

  if (keyDown)
    keydown(key);
  else
    keyup(key);
}

} // namespace ps2dev