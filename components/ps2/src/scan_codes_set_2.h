#ifndef DD85C2BD_1EA1_416E_B227_80C3C8C3E40A
#define DD85C2BD_1EA1_416E_B227_80C3C8C3E40A

//#include "Arduino.h"
#include <stdint.h>

// Source: http://www.computer-engineering.org/ps2keyboard/scancodes2.html
// Archive: https://web.archive.org/web/20100225093757/http://www.computer-engineering.org/ps2keyboard/scancodes2.html

namespace ps2dev {

namespace scancodes {

typedef enum {
  K_A,
  K_B,
  K_C,
  K_D,
  K_E,
  K_F,
  K_G,
  K_H,
  K_I,
  K_J,
  K_K,
  K_L,
  K_M,
  K_N,
  K_O,
  K_P,
  K_Q,
  K_R,
  K_S,
  K_T,
  K_U,
  K_V,
  K_W,
  K_X,
  K_Y,
  K_Z,
  K_0,
  K_1,
  K_2,
  K_3,
  K_4,
  K_5,
  K_6,
  K_7,
  K_8,
  K_9,
  K_BACKQUOTE,
  K_MINUS,
  K_EQUALS,
  K_BACKSLASH,
  K_BACKSPACE,
  K_SPACE,
  K_TAB,
  K_CAPSLOCK,
  K_LSHIFT,
  K_LCTRL,
  K_LSUPER,
  K_LALT,
  K_RSHIFT,
  K_RCTRL,
  K_RSUPER,
  K_RALT,
  K_MENU,
  K_RETURN,
  K_ESCAPE,
  K_F1,
  K_F2,
  K_F3,
  K_F4,
  K_F5,
  K_F6,
  K_F7,
  K_F8,
  K_F9,
  K_F10,
  K_F11,
  K_F12,
  K_PRINT,
  K_SCROLLOCK,
  K_PAUSE,
  K_LEFTBRACKET,
  K_INSERT,
  K_HOME,
  K_PAGEUP,
  K_DELETE,
  K_END,
  K_PAGEDOWN,
  K_UP,
  K_LEFT,
  K_DOWN,
  K_RIGHT,
  K_NUMLOCK,
  K_KP_DIVIDE,
  K_KP_MULTIPLY,
  K_KP_MINUS,
  K_KP_PLUS,
  K_KP_ENTER,
  K_KP_PERIOD,
  K_KP0,
  K_KP1,
  K_KP2,
  K_KP3,
  K_KP4,
  K_KP5,
  K_KP6,
  K_KP7,
  K_KP8,
  K_KP9,
  K_RIGHTBRACKET,
  K_SEMICOLON,
  K_QUOTE,
  K_COMMA,
  K_PERIOD,
  K_SLASH,
  K_ACPI_POWER,
  K_ACPI_SLEEP,
  K_ACPI_WAKE,
  K_MEDIA_NEXT_TRACK,
  K_MEDIA_PREV_TRACK,
  K_MEDIA_STOP,
  K_MEDIA_PLAY_PAUSE,
  K_MEDIA_MUTE,
  K_MEDIA_VOLUME_UP,
  K_MEDIA_VOLUME_DOWN,
  K_MEDIA_MEDIA_SELECT,
  K_MEDIA_EMAIL,
  K_MEDIA_CALC,
  K_MEDIA_MY_COMPUTER,
  K_MEDIA_WWW_SEARCH,
  K_MEDIA_WWW_HOME,
  K_MEDIA_WWW_BACK,
  K_MEDIA_WWW_FORWARD,
  K_MEDIA_WWW_STOP,
  K_MEDIA_WWW_REFRESH,
  K_MEDIA_WWW_FAVORITES,
} Key;

const uint8_t MAKE_K_A[] = {0x1C};
const uint8_t MAKE_K_B[] = {0x32};
const uint8_t MAKE_K_C[] = {0x21};
const uint8_t MAKE_K_D[] = {0x23};
const uint8_t MAKE_K_E[] = {0x24};
const uint8_t MAKE_K_F[] = {0x2B};
const uint8_t MAKE_K_G[] = {0x34};
const uint8_t MAKE_K_H[] = {0x33};
const uint8_t MAKE_K_I[] = {0x43};
const uint8_t MAKE_K_J[] = {0x3B};
const uint8_t MAKE_K_K[] = {0x42};
const uint8_t MAKE_K_L[] = {0x4B};
const uint8_t MAKE_K_M[] = {0x3A};
const uint8_t MAKE_K_N[] = {0x31};
const uint8_t MAKE_K_O[] = {0x44};
const uint8_t MAKE_K_P[] = {0x4D};
const uint8_t MAKE_K_Q[] = {0x15};
const uint8_t MAKE_K_R[] = {0x2D};
const uint8_t MAKE_K_S[] = {0x1B};
const uint8_t MAKE_K_T[] = {0x2C};
const uint8_t MAKE_K_U[] = {0x3C};
const uint8_t MAKE_K_V[] = {0x2A};
const uint8_t MAKE_K_W[] = {0x1D};
const uint8_t MAKE_K_X[] = {0x22};
const uint8_t MAKE_K_Y[] = {0x35};
const uint8_t MAKE_K_Z[] = {0x1A};
const uint8_t MAKE_K_0[] = {0x45};
const uint8_t MAKE_K_1[] = {0x16};
const uint8_t MAKE_K_2[] = {0x1E};
const uint8_t MAKE_K_3[] = {0x26};
const uint8_t MAKE_K_4[] = {0x25};
const uint8_t MAKE_K_5[] = {0x2E};
const uint8_t MAKE_K_6[] = {0x36};
const uint8_t MAKE_K_7[] = {0x3D};
const uint8_t MAKE_K_8[] = {0x3E};
const uint8_t MAKE_K_9[] = {0x46};
const uint8_t MAKE_K_BACKQUOTE[] = {0x0E};
const uint8_t MAKE_K_MINUS[] = {0x4E};
const uint8_t MAKE_K_EQUALS[] = {0x55};
const uint8_t MAKE_K_BACKSLASH[] = {0x5D};
const uint8_t MAKE_K_BACKSPACE[] = {0x66};
const uint8_t MAKE_K_SPACE[] = {0x29};
const uint8_t MAKE_K_TAB[] = {0x0D};
const uint8_t MAKE_K_CAPSLOCK[] = {0x58};
const uint8_t MAKE_K_LSHIFT[] = {0x12};
const uint8_t MAKE_K_LCTRL[] = {0x14};
const uint8_t MAKE_K_LSUPER[] = {0xE0, 0x1F};
const uint8_t MAKE_K_LALT[] = {0x11};
const uint8_t MAKE_K_RSHIFT[] = {0x59};
const uint8_t MAKE_K_RCTRL[] = {0xE0, 0x14};
const uint8_t MAKE_K_RSUPER[] = {0xE0, 0x27};
const uint8_t MAKE_K_RALT[] = {0xE0, 0x11};
const uint8_t MAKE_K_MENU[] = {0xE0, 0x2F};
const uint8_t MAKE_K_RETURN[] = {0x5A};
const uint8_t MAKE_K_ESCAPE[] = {0x76};
const uint8_t MAKE_K_F1[] = {0x05};
const uint8_t MAKE_K_F2[] = {0x06};
const uint8_t MAKE_K_F3[] = {0x04};
const uint8_t MAKE_K_F4[] = {0x0C};
const uint8_t MAKE_K_F5[] = {0x03};
const uint8_t MAKE_K_F6[] = {0x0B};
const uint8_t MAKE_K_F7[] = {0x83};
const uint8_t MAKE_K_F8[] = {0x0A};
const uint8_t MAKE_K_F9[] = {0x01};
const uint8_t MAKE_K_F10[] = {0x09};
const uint8_t MAKE_K_F11[] = {0x78};
const uint8_t MAKE_K_F12[] = {0x07};
const uint8_t MAKE_K_PRINT[] = {0xE0, 0x12, 0xE0, 0x7C};
const uint8_t MAKE_K_SCROLLOCK[] = {0x7E};
const uint8_t MAKE_K_PAUSE[] = {0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77};
const uint8_t MAKE_K_LEFTBRACKET[] = {0x54};
const uint8_t MAKE_K_INSERT[] = {0xE0, 0x70};
const uint8_t MAKE_K_HOME[] = {0xE0, 0x6C};
const uint8_t MAKE_K_PAGEUP[] = {0xE0, 0x7D};
const uint8_t MAKE_K_DELETE[] = {0xE0, 0x71};
const uint8_t MAKE_K_END[] = {0xE0, 0x69};
const uint8_t MAKE_K_PAGEDOWN[] = {0xE0, 0x7A};
const uint8_t MAKE_K_UP[] = {0xE0, 0x75};
const uint8_t MAKE_K_LEFT[] = {0xE0, 0x6B};
const uint8_t MAKE_K_DOWN[] = {0xE0, 0x72};
const uint8_t MAKE_K_RIGHT[] = {0xE0, 0x74};
const uint8_t MAKE_K_NUMLOCK[] = {0x77};
const uint8_t MAKE_K_KP_DIVIDE[] = {0xE0, 0x4A};
const uint8_t MAKE_K_KP_MULTIPLY[] = {0x7C};
const uint8_t MAKE_K_KP_MINUS[] = {0x7B};
const uint8_t MAKE_K_KP_PLUS[] = {0x79};
const uint8_t MAKE_K_KP_ENTER[] = {0xE0, 0x5A};
const uint8_t MAKE_K_KP_PERIOD[] = {0x71};
const uint8_t MAKE_K_KP0[] = {0x70};
const uint8_t MAKE_K_KP1[] = {0x69};
const uint8_t MAKE_K_KP2[] = {0x72};
const uint8_t MAKE_K_KP3[] = {0x7A};
const uint8_t MAKE_K_KP4[] = {0x6B};
const uint8_t MAKE_K_KP5[] = {0x73};
const uint8_t MAKE_K_KP6[] = {0x74};
const uint8_t MAKE_K_KP7[] = {0x6C};
const uint8_t MAKE_K_KP8[] = {0x75};
const uint8_t MAKE_K_KP9[] = {0x7D};
const uint8_t MAKE_K_RIGHTBRACKET[] = {0x5B};
const uint8_t MAKE_K_SEMICOLON[] = {0x4C};
const uint8_t MAKE_K_QUOTE[] = {0x52};
const uint8_t MAKE_K_COMMA[] = {0x41};
const uint8_t MAKE_K_PERIOD[] = {0x49};
const uint8_t MAKE_K_SLASH[] = {0x4A};
const uint8_t MAKE_K_ACPI_POWER[] = {0xE0, 0x37};
const uint8_t MAKE_K_ACPI_SLEEP[] = {0xE0, 0x3F};
const uint8_t MAKE_K_ACPI_WAKE[] = {0xE0, 0x5E};
const uint8_t MAKE_K_MEDIA_NEXT_TRACK[] = {0xE0, 0x4D};
const uint8_t MAKE_K_MEDIA_PREV_TRACK[] = {0xE0, 0x15};
const uint8_t MAKE_K_MEDIA_STOP[] = {0xE0, 0x3B};
const uint8_t MAKE_K_MEDIA_PLAY_PAUSE[] = {0xE0, 0x34};
const uint8_t MAKE_K_MEDIA_MUTE[] = {0xE0, 0x23};
const uint8_t MAKE_K_MEDIA_VOLUME_UP[] = {0xE0, 0x32};
const uint8_t MAKE_K_MEDIA_VOLUME_DOWN[] = {0xE0, 0x21};
const uint8_t MAKE_K_MEDIA_MEDIA_SELECT[] = {0xE0, 0x50};
const uint8_t MAKE_K_MEDIA_EMAIL[] = {0xE0, 0x48};
const uint8_t MAKE_K_MEDIA_CALC[] = {0xE0, 0x2B};
const uint8_t MAKE_K_MEDIA_MY_COMPUTER[] = {0xE0, 0x40};
const uint8_t MAKE_K_MEDIA_WWW_SEARCH[] = {0xE0, 0x10};
const uint8_t MAKE_K_MEDIA_WWW_HOME[] = {0xE0, 0x3A};
const uint8_t MAKE_K_MEDIA_WWW_BACK[] = {0xE0, 0x38};
const uint8_t MAKE_K_MEDIA_WWW_FORWARD[] = {0xE0, 0x30};
const uint8_t MAKE_K_MEDIA_WWW_STOP[] = {0xE0, 0x28};
const uint8_t MAKE_K_MEDIA_WWW_REFRESH[] = {0xE0, 0x20};
const uint8_t MAKE_K_MEDIA_WWW_FAVORITES[] = {0xE0, 0x18};

const uint8_t BREAK_K_A[] = {0xF0, 0x1C};
const uint8_t BREAK_K_B[] = {0xF0, 0x32};
const uint8_t BREAK_K_C[] = {0xF0, 0x21};
const uint8_t BREAK_K_D[] = {0xF0, 0x23};
const uint8_t BREAK_K_E[] = {0xF0, 0x24};
const uint8_t BREAK_K_F[] = {0xF0, 0x2B};
const uint8_t BREAK_K_G[] = {0xF0, 0x34};
const uint8_t BREAK_K_H[] = {0xF0, 0x33};
const uint8_t BREAK_K_I[] = {0xF0, 0x43};
const uint8_t BREAK_K_J[] = {0xF0, 0x3B};
const uint8_t BREAK_K_K[] = {0xF0, 0x42};
const uint8_t BREAK_K_L[] = {0xF0, 0x4B};
const uint8_t BREAK_K_M[] = {0xF0, 0x3A};
const uint8_t BREAK_K_N[] = {0xF0, 0x31};
const uint8_t BREAK_K_O[] = {0xF0, 0x44};
const uint8_t BREAK_K_P[] = {0xF0, 0x4D};
const uint8_t BREAK_K_Q[] = {0xF0, 0x15};
const uint8_t BREAK_K_R[] = {0xF0, 0x2D};
const uint8_t BREAK_K_S[] = {0xF0, 0x1B};
const uint8_t BREAK_K_T[] = {0xF0, 0x2C};
const uint8_t BREAK_K_U[] = {0xF0, 0x3C};
const uint8_t BREAK_K_V[] = {0xF0, 0x2A};
const uint8_t BREAK_K_W[] = {0xF0, 0x1D};
const uint8_t BREAK_K_X[] = {0xF0, 0x22};
const uint8_t BREAK_K_Y[] = {0xF0, 0x35};
const uint8_t BREAK_K_Z[] = {0xF0, 0x1A};
const uint8_t BREAK_K_0[] = {0xF0, 0x45};
const uint8_t BREAK_K_1[] = {0xF0, 0x16};
const uint8_t BREAK_K_2[] = {0xF0, 0x1E};
const uint8_t BREAK_K_3[] = {0xF0, 0x26};
const uint8_t BREAK_K_4[] = {0xF0, 0x25};
const uint8_t BREAK_K_5[] = {0xF0, 0x2E};
const uint8_t BREAK_K_6[] = {0xF0, 0x36};
const uint8_t BREAK_K_7[] = {0xF0, 0x3D};
const uint8_t BREAK_K_8[] = {0xF0, 0x3E};
const uint8_t BREAK_K_9[] = {0xF0, 0x46};
const uint8_t BREAK_K_BACKQUOTE[] = {0xF0, 0x0E};
const uint8_t BREAK_K_MINUS[] = {0xF0, 0x4E};
const uint8_t BREAK_K_EQUALS[] = {0xF0, 0x55};
const uint8_t BREAK_K_BACKSLASH[] = {0xF0, 0x5D};
const uint8_t BREAK_K_BACKSPACE[] = {0xF0, 0x66};
const uint8_t BREAK_K_SPACE[] = {0xF0, 0x29};
const uint8_t BREAK_K_TAB[] = {0xF0, 0x0D};
const uint8_t BREAK_K_CAPSLOCK[] = {0xF0, 0x58};
const uint8_t BREAK_K_LSHIFT[] = {0xF0, 0x12};
const uint8_t BREAK_K_LCTRL[] = {0xF0, 0x14};
const uint8_t BREAK_K_LSUPER[] = {0xE0, 0xF0, 0x1F};
const uint8_t BREAK_K_LALT[] = {0xF0, 0x11};
const uint8_t BREAK_K_RSHIFT[] = {0xF0, 0x59};
const uint8_t BREAK_K_RCTRL[] = {0xE0, 0xF0, 0x14};
const uint8_t BREAK_K_RSUPER[] = {0xE0, 0xF0, 0x27};
const uint8_t BREAK_K_RALT[] = {0xE0, 0xF0, 0x11};
const uint8_t BREAK_K_MENU[] = {0xE0, 0xF0, 0x2F};
const uint8_t BREAK_K_RETURN[] = {0xF0, 0x5A};
const uint8_t BREAK_K_ESCAPE[] = {0xF0, 0x76};
const uint8_t BREAK_K_F1[] = {0xF0, 0x05};
const uint8_t BREAK_K_F2[] = {0xF0, 0x06};
const uint8_t BREAK_K_F3[] = {0xF0, 0x04};
const uint8_t BREAK_K_F4[] = {0xF0, 0x0C};
const uint8_t BREAK_K_F5[] = {0xF0, 0x03};
const uint8_t BREAK_K_F6[] = {0xF0, 0x0B};
const uint8_t BREAK_K_F7[] = {0xF0, 0x83};
const uint8_t BREAK_K_F8[] = {0xF0, 0x0A};
const uint8_t BREAK_K_F9[] = {0xF0, 0x01};
const uint8_t BREAK_K_F10[] = {0xF0, 0x09};
const uint8_t BREAK_K_F11[] = {0xF0, 0x78};
const uint8_t BREAK_K_F12[] = {0xF0, 0x07};
const uint8_t BREAK_K_PRINT[] = {0xE0, 0xF0, 0x7C, 0xE0, 0xF0, 0x12};
const uint8_t BREAK_K_SCROLLOCK[] = {0xF0, 0x7E};
const uint8_t BREAK_K_PAUSE[] = {};
const uint8_t BREAK_K_LEFTBRACKET[] = {0xF0, 0x54};
const uint8_t BREAK_K_INSERT[] = {0xE0, 0xF0, 0x70};
const uint8_t BREAK_K_HOME[] = {0xE0, 0xF0, 0x6C};
const uint8_t BREAK_K_PAGEUP[] = {0xE0, 0xF0, 0x7D};
const uint8_t BREAK_K_DELETE[] = {0xE0, 0xF0, 0x71};
const uint8_t BREAK_K_END[] = {0xE0, 0xF0, 0x69};
const uint8_t BREAK_K_PAGEDOWN[] = {0xE0, 0xF0, 0x7A};
const uint8_t BREAK_K_UP[] = {0xE0, 0xF0, 0x75};
const uint8_t BREAK_K_LEFT[] = {0xE0, 0xF0, 0x6B};
const uint8_t BREAK_K_DOWN[] = {0xE0, 0xF0, 0x72};
const uint8_t BREAK_K_RIGHT[] = {0xE0, 0xF0, 0x74};
const uint8_t BREAK_K_NUMLOCK[] = {0xF0, 0x77};
const uint8_t BREAK_K_KP_DIVIDE[] = {0xE0, 0xF0, 0x4A};
const uint8_t BREAK_K_KP_MULTIPLY[] = {0xF0, 0x7C};
const uint8_t BREAK_K_KP_MINUS[] = {0xF0, 0x7B};
const uint8_t BREAK_K_KP_PLUS[] = {0xF0, 0x79};
const uint8_t BREAK_K_KP_ENTER[] = {0xE0, 0xF0, 0x5A};
const uint8_t BREAK_K_KP_PERIOD[] = {0xF0, 0x71};
const uint8_t BREAK_K_KP0[] = {0xF0, 0x70};
const uint8_t BREAK_K_KP1[] = {0xF0, 0x69};
const uint8_t BREAK_K_KP2[] = {0xF0, 0x72};
const uint8_t BREAK_K_KP3[] = {0xF0, 0x7A};
const uint8_t BREAK_K_KP4[] = {0xF0, 0x6B};
const uint8_t BREAK_K_KP5[] = {0xF0, 0x73};
const uint8_t BREAK_K_KP6[] = {0xF0, 0x74};
const uint8_t BREAK_K_KP7[] = {0xF0, 0x6C};
const uint8_t BREAK_K_KP8[] = {0xF0, 0x75};
const uint8_t BREAK_K_KP9[] = {0xF0, 0x7D};
const uint8_t BREAK_K_RIGHTBRACKET[] = {0xF0, 0x5B};
const uint8_t BREAK_K_SEMICOLON[] = {0xF0, 0x4C};
const uint8_t BREAK_K_QUOTE[] = {0xF0, 0x52};
const uint8_t BREAK_K_COMMA[] = {0xF0, 0x41};
const uint8_t BREAK_K_PERIOD[] = {0xF0, 0x49};
const uint8_t BREAK_K_SLASH[] = {0xF0, 0x4A};
const uint8_t BREAK_K_ACPI_POWER[] = {0xE0, 0xF0, 0x37};
const uint8_t BREAK_K_ACPI_SLEEP[] = {0xE0, 0xF0, 0x3F};
const uint8_t BREAK_K_ACPI_WAKE[] = {0xE0, 0xF0, 0x5E};
const uint8_t BREAK_K_MEDIA_NEXT_TRACK[] = {0xE0, 0xF0, 0x4D};
const uint8_t BREAK_K_MEDIA_PREV_TRACK[] = {0xE0, 0xF0, 0x15};
const uint8_t BREAK_K_MEDIA_STOP[] = {0xE0, 0xF0, 0x3B};
const uint8_t BREAK_K_MEDIA_PLAY_PAUSE[] = {0xE0, 0xF0, 0x34};
const uint8_t BREAK_K_MEDIA_MUTE[] = {0xE0, 0xF0, 0x23};
const uint8_t BREAK_K_MEDIA_VOLUME_UP[] = {0xE0, 0xF0, 0x32};
const uint8_t BREAK_K_MEDIA_VOLUME_DOWN[] = {0xE0, 0xF0, 0x21};
const uint8_t BREAK_K_MEDIA_MEDIA_SELECT[] = {0xE0, 0xF0, 0x50};
const uint8_t BREAK_K_MEDIA_EMAIL[] = {0xE0, 0xF0, 0x48};
const uint8_t BREAK_K_MEDIA_CALC[] = {0xE0, 0xF0, 0x2B};
const uint8_t BREAK_K_MEDIA_MY_COMPUTER[] = {0xE0, 0xF0, 0x40};
const uint8_t BREAK_K_MEDIA_WWW_SEARCH[] = {0xE0, 0xF0, 0x10};
const uint8_t BREAK_K_MEDIA_WWW_HOME[] = {0xE0, 0xF0, 0x3A};
const uint8_t BREAK_K_MEDIA_WWW_BACK[] = {0xE0, 0xF0, 0x38};
const uint8_t BREAK_K_MEDIA_WWW_FORWARD[] = {0xE0, 0xF0, 0x30};
const uint8_t BREAK_K_MEDIA_WWW_STOP[] = {0xE0, 0xF0, 0x28};
const uint8_t BREAK_K_MEDIA_WWW_REFRESH[] = {0xE0, 0xF0, 0x20};
const uint8_t BREAK_K_MEDIA_WWW_FAVORITES[] = {0xE0, 0xF0, 0x18};

const uint8_t* const MAKE_CODES[] = {MAKE_K_A,
                                     MAKE_K_B,
                                     MAKE_K_C,
                                     MAKE_K_D,
                                     MAKE_K_E,
                                     MAKE_K_F,
                                     MAKE_K_G,
                                     MAKE_K_H,
                                     MAKE_K_I,
                                     MAKE_K_J,
                                     MAKE_K_K,
                                     MAKE_K_L,
                                     MAKE_K_M,
                                     MAKE_K_N,
                                     MAKE_K_O,
                                     MAKE_K_P,
                                     MAKE_K_Q,
                                     MAKE_K_R,
                                     MAKE_K_S,
                                     MAKE_K_T,
                                     MAKE_K_U,
                                     MAKE_K_V,
                                     MAKE_K_W,
                                     MAKE_K_X,
                                     MAKE_K_Y,
                                     MAKE_K_Z,
                                     MAKE_K_0,
                                     MAKE_K_1,
                                     MAKE_K_2,
                                     MAKE_K_3,
                                     MAKE_K_4,
                                     MAKE_K_5,
                                     MAKE_K_6,
                                     MAKE_K_7,
                                     MAKE_K_8,
                                     MAKE_K_9,
                                     MAKE_K_BACKQUOTE,
                                     MAKE_K_MINUS,
                                     MAKE_K_EQUALS,
                                     MAKE_K_BACKSLASH,
                                     MAKE_K_BACKSPACE,
                                     MAKE_K_SPACE,
                                     MAKE_K_TAB,
                                     MAKE_K_CAPSLOCK,
                                     MAKE_K_LSHIFT,
                                     MAKE_K_LCTRL,
                                     MAKE_K_LSUPER,
                                     MAKE_K_LALT,
                                     MAKE_K_RSHIFT,
                                     MAKE_K_RCTRL,
                                     MAKE_K_RSUPER,
                                     MAKE_K_RALT,
                                     MAKE_K_MENU,
                                     MAKE_K_RETURN,
                                     MAKE_K_ESCAPE,
                                     MAKE_K_F1,
                                     MAKE_K_F2,
                                     MAKE_K_F3,
                                     MAKE_K_F4,
                                     MAKE_K_F5,
                                     MAKE_K_F6,
                                     MAKE_K_F7,
                                     MAKE_K_F8,
                                     MAKE_K_F9,
                                     MAKE_K_F10,
                                     MAKE_K_F11,
                                     MAKE_K_F12,
                                     MAKE_K_PRINT,
                                     MAKE_K_SCROLLOCK,
                                     MAKE_K_PAUSE,
                                     MAKE_K_LEFTBRACKET,
                                     MAKE_K_INSERT,
                                     MAKE_K_HOME,
                                     MAKE_K_PAGEUP,
                                     MAKE_K_DELETE,
                                     MAKE_K_END,
                                     MAKE_K_PAGEDOWN,
                                     MAKE_K_UP,
                                     MAKE_K_LEFT,
                                     MAKE_K_DOWN,
                                     MAKE_K_RIGHT,
                                     MAKE_K_NUMLOCK,
                                     MAKE_K_KP_DIVIDE,
                                     MAKE_K_KP_MULTIPLY,
                                     MAKE_K_KP_MINUS,
                                     MAKE_K_KP_PLUS,
                                     MAKE_K_KP_ENTER,
                                     MAKE_K_KP_PERIOD,
                                     MAKE_K_KP0,
                                     MAKE_K_KP1,
                                     MAKE_K_KP2,
                                     MAKE_K_KP3,
                                     MAKE_K_KP4,
                                     MAKE_K_KP5,
                                     MAKE_K_KP6,
                                     MAKE_K_KP7,
                                     MAKE_K_KP8,
                                     MAKE_K_KP9,
                                     MAKE_K_RIGHTBRACKET,
                                     MAKE_K_SEMICOLON,
                                     MAKE_K_QUOTE,
                                     MAKE_K_COMMA,
                                     MAKE_K_PERIOD,
                                     MAKE_K_SLASH,
                                     MAKE_K_ACPI_POWER,
                                     MAKE_K_ACPI_SLEEP,
                                     MAKE_K_ACPI_WAKE,
                                     MAKE_K_MEDIA_NEXT_TRACK,
                                     MAKE_K_MEDIA_PREV_TRACK,
                                     MAKE_K_MEDIA_STOP,
                                     MAKE_K_MEDIA_PLAY_PAUSE,
                                     MAKE_K_MEDIA_MUTE,
                                     MAKE_K_MEDIA_VOLUME_UP,
                                     MAKE_K_MEDIA_VOLUME_DOWN,
                                     MAKE_K_MEDIA_MEDIA_SELECT,
                                     MAKE_K_MEDIA_EMAIL,
                                     MAKE_K_MEDIA_CALC,
                                     MAKE_K_MEDIA_MY_COMPUTER,
                                     MAKE_K_MEDIA_WWW_SEARCH,
                                     MAKE_K_MEDIA_WWW_HOME,
                                     MAKE_K_MEDIA_WWW_BACK,
                                     MAKE_K_MEDIA_WWW_FORWARD,
                                     MAKE_K_MEDIA_WWW_STOP,
                                     MAKE_K_MEDIA_WWW_REFRESH,
                                     MAKE_K_MEDIA_WWW_FAVORITES};

const uint8_t MAKE_CODES_LEN[] = {sizeof(MAKE_K_A),
                                  sizeof(MAKE_K_B),
                                  sizeof(MAKE_K_C),
                                  sizeof(MAKE_K_D),
                                  sizeof(MAKE_K_E),
                                  sizeof(MAKE_K_F),
                                  sizeof(MAKE_K_G),
                                  sizeof(MAKE_K_H),
                                  sizeof(MAKE_K_I),
                                  sizeof(MAKE_K_J),
                                  sizeof(MAKE_K_K),
                                  sizeof(MAKE_K_L),
                                  sizeof(MAKE_K_M),
                                  sizeof(MAKE_K_N),
                                  sizeof(MAKE_K_O),
                                  sizeof(MAKE_K_P),
                                  sizeof(MAKE_K_Q),
                                  sizeof(MAKE_K_R),
                                  sizeof(MAKE_K_S),
                                  sizeof(MAKE_K_T),
                                  sizeof(MAKE_K_U),
                                  sizeof(MAKE_K_V),
                                  sizeof(MAKE_K_W),
                                  sizeof(MAKE_K_X),
                                  sizeof(MAKE_K_Y),
                                  sizeof(MAKE_K_Z),
                                  sizeof(MAKE_K_0),
                                  sizeof(MAKE_K_1),
                                  sizeof(MAKE_K_2),
                                  sizeof(MAKE_K_3),
                                  sizeof(MAKE_K_4),
                                  sizeof(MAKE_K_5),
                                  sizeof(MAKE_K_6),
                                  sizeof(MAKE_K_7),
                                  sizeof(MAKE_K_8),
                                  sizeof(MAKE_K_9),
                                  sizeof(MAKE_K_BACKQUOTE),
                                  sizeof(MAKE_K_MINUS),
                                  sizeof(MAKE_K_EQUALS),
                                  sizeof(MAKE_K_BACKSLASH),
                                  sizeof(MAKE_K_BACKSPACE),
                                  sizeof(MAKE_K_SPACE),
                                  sizeof(MAKE_K_TAB),
                                  sizeof(MAKE_K_CAPSLOCK),
                                  sizeof(MAKE_K_LSHIFT),
                                  sizeof(MAKE_K_LCTRL),
                                  sizeof(MAKE_K_LSUPER),
                                  sizeof(MAKE_K_LALT),
                                  sizeof(MAKE_K_RSHIFT),
                                  sizeof(MAKE_K_RCTRL),
                                  sizeof(MAKE_K_RSUPER),
                                  sizeof(MAKE_K_RALT),
                                  sizeof(MAKE_K_MENU),
                                  sizeof(MAKE_K_RETURN),
                                  sizeof(MAKE_K_ESCAPE),
                                  sizeof(MAKE_K_F1),
                                  sizeof(MAKE_K_F2),
                                  sizeof(MAKE_K_F3),
                                  sizeof(MAKE_K_F4),
                                  sizeof(MAKE_K_F5),
                                  sizeof(MAKE_K_F6),
                                  sizeof(MAKE_K_F7),
                                  sizeof(MAKE_K_F8),
                                  sizeof(MAKE_K_F9),
                                  sizeof(MAKE_K_F10),
                                  sizeof(MAKE_K_F11),
                                  sizeof(MAKE_K_F12),
                                  sizeof(MAKE_K_PRINT),
                                  sizeof(MAKE_K_SCROLLOCK),
                                  sizeof(MAKE_K_PAUSE),
                                  sizeof(MAKE_K_LEFTBRACKET),
                                  sizeof(MAKE_K_INSERT),
                                  sizeof(MAKE_K_HOME),
                                  sizeof(MAKE_K_PAGEUP),
                                  sizeof(MAKE_K_DELETE),
                                  sizeof(MAKE_K_END),
                                  sizeof(MAKE_K_PAGEDOWN),
                                  sizeof(MAKE_K_UP),
                                  sizeof(MAKE_K_LEFT),
                                  sizeof(MAKE_K_DOWN),
                                  sizeof(MAKE_K_RIGHT),
                                  sizeof(MAKE_K_NUMLOCK),
                                  sizeof(MAKE_K_KP_DIVIDE),
                                  sizeof(MAKE_K_KP_MULTIPLY),
                                  sizeof(MAKE_K_KP_MINUS),
                                  sizeof(MAKE_K_KP_PLUS),
                                  sizeof(MAKE_K_KP_ENTER),
                                  sizeof(MAKE_K_KP_PERIOD),
                                  sizeof(MAKE_K_KP0),
                                  sizeof(MAKE_K_KP1),
                                  sizeof(MAKE_K_KP2),
                                  sizeof(MAKE_K_KP3),
                                  sizeof(MAKE_K_KP4),
                                  sizeof(MAKE_K_KP5),
                                  sizeof(MAKE_K_KP6),
                                  sizeof(MAKE_K_KP7),
                                  sizeof(MAKE_K_KP8),
                                  sizeof(MAKE_K_KP9),
                                  sizeof(MAKE_K_RIGHTBRACKET),
                                  sizeof(MAKE_K_SEMICOLON),
                                  sizeof(MAKE_K_QUOTE),
                                  sizeof(MAKE_K_COMMA),
                                  sizeof(MAKE_K_PERIOD),
                                  sizeof(MAKE_K_SLASH),
                                  sizeof(MAKE_K_ACPI_POWER),
                                  sizeof(MAKE_K_ACPI_SLEEP),
                                  sizeof(MAKE_K_ACPI_WAKE),
                                  sizeof(MAKE_K_MEDIA_NEXT_TRACK),
                                  sizeof(MAKE_K_MEDIA_PREV_TRACK),
                                  sizeof(MAKE_K_MEDIA_STOP),
                                  sizeof(MAKE_K_MEDIA_PLAY_PAUSE),
                                  sizeof(MAKE_K_MEDIA_MUTE),
                                  sizeof(MAKE_K_MEDIA_VOLUME_UP),
                                  sizeof(MAKE_K_MEDIA_VOLUME_DOWN),
                                  sizeof(MAKE_K_MEDIA_MEDIA_SELECT),
                                  sizeof(MAKE_K_MEDIA_EMAIL),
                                  sizeof(MAKE_K_MEDIA_CALC),
                                  sizeof(MAKE_K_MEDIA_MY_COMPUTER),
                                  sizeof(MAKE_K_MEDIA_WWW_SEARCH),
                                  sizeof(MAKE_K_MEDIA_WWW_HOME),
                                  sizeof(MAKE_K_MEDIA_WWW_BACK),
                                  sizeof(MAKE_K_MEDIA_WWW_FORWARD),
                                  sizeof(MAKE_K_MEDIA_WWW_STOP),
                                  sizeof(MAKE_K_MEDIA_WWW_REFRESH),
                                  sizeof(MAKE_K_MEDIA_WWW_FAVORITES)};

const uint8_t* const BREAK_CODES[] = {BREAK_K_A,
                                      BREAK_K_B,
                                      BREAK_K_C,
                                      BREAK_K_D,
                                      BREAK_K_E,
                                      BREAK_K_F,
                                      BREAK_K_G,
                                      BREAK_K_H,
                                      BREAK_K_I,
                                      BREAK_K_J,
                                      BREAK_K_K,
                                      BREAK_K_L,
                                      BREAK_K_M,
                                      BREAK_K_N,
                                      BREAK_K_O,
                                      BREAK_K_P,
                                      BREAK_K_Q,
                                      BREAK_K_R,
                                      BREAK_K_S,
                                      BREAK_K_T,
                                      BREAK_K_U,
                                      BREAK_K_V,
                                      BREAK_K_W,
                                      BREAK_K_X,
                                      BREAK_K_Y,
                                      BREAK_K_Z,
                                      BREAK_K_0,
                                      BREAK_K_1,
                                      BREAK_K_2,
                                      BREAK_K_3,
                                      BREAK_K_4,
                                      BREAK_K_5,
                                      BREAK_K_6,
                                      BREAK_K_7,
                                      BREAK_K_8,
                                      BREAK_K_9,
                                      BREAK_K_BACKQUOTE,
                                      BREAK_K_MINUS,
                                      BREAK_K_EQUALS,
                                      BREAK_K_BACKSLASH,
                                      BREAK_K_BACKSPACE,
                                      BREAK_K_SPACE,
                                      BREAK_K_TAB,
                                      BREAK_K_CAPSLOCK,
                                      BREAK_K_LSHIFT,
                                      BREAK_K_LCTRL,
                                      BREAK_K_LSUPER,
                                      BREAK_K_LALT,
                                      BREAK_K_RSHIFT,
                                      BREAK_K_RCTRL,
                                      BREAK_K_RSUPER,
                                      BREAK_K_RALT,
                                      BREAK_K_MENU,
                                      BREAK_K_RETURN,
                                      BREAK_K_ESCAPE,
                                      BREAK_K_F1,
                                      BREAK_K_F2,
                                      BREAK_K_F3,
                                      BREAK_K_F4,
                                      BREAK_K_F5,
                                      BREAK_K_F6,
                                      BREAK_K_F7,
                                      BREAK_K_F8,
                                      BREAK_K_F9,
                                      BREAK_K_F10,
                                      BREAK_K_F11,
                                      BREAK_K_F12,
                                      BREAK_K_PRINT,
                                      BREAK_K_SCROLLOCK,
                                      BREAK_K_PAUSE,
                                      BREAK_K_LEFTBRACKET,
                                      BREAK_K_INSERT,
                                      BREAK_K_HOME,
                                      BREAK_K_PAGEUP,
                                      BREAK_K_DELETE,
                                      BREAK_K_END,
                                      BREAK_K_PAGEDOWN,
                                      BREAK_K_UP,
                                      BREAK_K_LEFT,
                                      BREAK_K_DOWN,
                                      BREAK_K_RIGHT,
                                      BREAK_K_NUMLOCK,
                                      BREAK_K_KP_DIVIDE,
                                      BREAK_K_KP_MULTIPLY,
                                      BREAK_K_KP_MINUS,
                                      BREAK_K_KP_PLUS,
                                      BREAK_K_KP_ENTER,
                                      BREAK_K_KP_PERIOD,
                                      BREAK_K_KP0,
                                      BREAK_K_KP1,
                                      BREAK_K_KP2,
                                      BREAK_K_KP3,
                                      BREAK_K_KP4,
                                      BREAK_K_KP5,
                                      BREAK_K_KP6,
                                      BREAK_K_KP7,
                                      BREAK_K_KP8,
                                      BREAK_K_KP9,
                                      BREAK_K_RIGHTBRACKET,
                                      BREAK_K_SEMICOLON,
                                      BREAK_K_QUOTE,
                                      BREAK_K_COMMA,
                                      BREAK_K_PERIOD,
                                      BREAK_K_SLASH,
                                      BREAK_K_ACPI_POWER,
                                      BREAK_K_ACPI_SLEEP,
                                      BREAK_K_ACPI_WAKE,
                                      BREAK_K_MEDIA_NEXT_TRACK,
                                      BREAK_K_MEDIA_PREV_TRACK,
                                      BREAK_K_MEDIA_STOP,
                                      BREAK_K_MEDIA_PLAY_PAUSE,
                                      BREAK_K_MEDIA_MUTE,
                                      BREAK_K_MEDIA_VOLUME_UP,
                                      BREAK_K_MEDIA_VOLUME_DOWN,
                                      BREAK_K_MEDIA_MEDIA_SELECT,
                                      BREAK_K_MEDIA_EMAIL,
                                      BREAK_K_MEDIA_CALC,
                                      BREAK_K_MEDIA_MY_COMPUTER,
                                      BREAK_K_MEDIA_WWW_SEARCH,
                                      BREAK_K_MEDIA_WWW_HOME,
                                      BREAK_K_MEDIA_WWW_BACK,
                                      BREAK_K_MEDIA_WWW_FORWARD,
                                      BREAK_K_MEDIA_WWW_STOP,
                                      BREAK_K_MEDIA_WWW_REFRESH,
                                      BREAK_K_MEDIA_WWW_FAVORITES};

const uint8_t BREAK_CODES_LEN[] = {sizeof(BREAK_K_A),
                                   sizeof(BREAK_K_B),
                                   sizeof(BREAK_K_C),
                                   sizeof(BREAK_K_D),
                                   sizeof(BREAK_K_E),
                                   sizeof(BREAK_K_F),
                                   sizeof(BREAK_K_G),
                                   sizeof(BREAK_K_H),
                                   sizeof(BREAK_K_I),
                                   sizeof(BREAK_K_J),
                                   sizeof(BREAK_K_K),
                                   sizeof(BREAK_K_L),
                                   sizeof(BREAK_K_M),
                                   sizeof(BREAK_K_N),
                                   sizeof(BREAK_K_O),
                                   sizeof(BREAK_K_P),
                                   sizeof(BREAK_K_Q),
                                   sizeof(BREAK_K_R),
                                   sizeof(BREAK_K_S),
                                   sizeof(BREAK_K_T),
                                   sizeof(BREAK_K_U),
                                   sizeof(BREAK_K_V),
                                   sizeof(BREAK_K_W),
                                   sizeof(BREAK_K_X),
                                   sizeof(BREAK_K_Y),
                                   sizeof(BREAK_K_Z),
                                   sizeof(BREAK_K_0),
                                   sizeof(BREAK_K_1),
                                   sizeof(BREAK_K_2),
                                   sizeof(BREAK_K_3),
                                   sizeof(BREAK_K_4),
                                   sizeof(BREAK_K_5),
                                   sizeof(BREAK_K_6),
                                   sizeof(BREAK_K_7),
                                   sizeof(BREAK_K_8),
                                   sizeof(BREAK_K_9),
                                   sizeof(BREAK_K_BACKQUOTE),
                                   sizeof(BREAK_K_MINUS),
                                   sizeof(BREAK_K_EQUALS),
                                   sizeof(BREAK_K_BACKSLASH),
                                   sizeof(BREAK_K_BACKSPACE),
                                   sizeof(BREAK_K_SPACE),
                                   sizeof(BREAK_K_TAB),
                                   sizeof(BREAK_K_CAPSLOCK),
                                   sizeof(BREAK_K_LSHIFT),
                                   sizeof(BREAK_K_LCTRL),
                                   sizeof(BREAK_K_LSUPER),
                                   sizeof(BREAK_K_LALT),
                                   sizeof(BREAK_K_RSHIFT),
                                   sizeof(BREAK_K_RCTRL),
                                   sizeof(BREAK_K_RSUPER),
                                   sizeof(BREAK_K_RALT),
                                   sizeof(BREAK_K_MENU),
                                   sizeof(BREAK_K_RETURN),
                                   sizeof(BREAK_K_ESCAPE),
                                   sizeof(BREAK_K_F1),
                                   sizeof(BREAK_K_F2),
                                   sizeof(BREAK_K_F3),
                                   sizeof(BREAK_K_F4),
                                   sizeof(BREAK_K_F5),
                                   sizeof(BREAK_K_F6),
                                   sizeof(BREAK_K_F7),
                                   sizeof(BREAK_K_F8),
                                   sizeof(BREAK_K_F9),
                                   sizeof(BREAK_K_F10),
                                   sizeof(BREAK_K_F11),
                                   sizeof(BREAK_K_F12),
                                   sizeof(BREAK_K_PRINT),
                                   sizeof(BREAK_K_SCROLLOCK),
                                   sizeof(BREAK_K_PAUSE),
                                   sizeof(BREAK_K_LEFTBRACKET),
                                   sizeof(BREAK_K_INSERT),
                                   sizeof(BREAK_K_HOME),
                                   sizeof(BREAK_K_PAGEUP),
                                   sizeof(BREAK_K_DELETE),
                                   sizeof(BREAK_K_END),
                                   sizeof(BREAK_K_PAGEDOWN),
                                   sizeof(BREAK_K_UP),
                                   sizeof(BREAK_K_LEFT),
                                   sizeof(BREAK_K_DOWN),
                                   sizeof(BREAK_K_RIGHT),
                                   sizeof(BREAK_K_NUMLOCK),
                                   sizeof(BREAK_K_KP_DIVIDE),
                                   sizeof(BREAK_K_KP_MULTIPLY),
                                   sizeof(BREAK_K_KP_MINUS),
                                   sizeof(BREAK_K_KP_PLUS),
                                   sizeof(BREAK_K_KP_ENTER),
                                   sizeof(BREAK_K_KP_PERIOD),
                                   sizeof(BREAK_K_KP0),
                                   sizeof(BREAK_K_KP1),
                                   sizeof(BREAK_K_KP2),
                                   sizeof(BREAK_K_KP3),
                                   sizeof(BREAK_K_KP4),
                                   sizeof(BREAK_K_KP5),
                                   sizeof(BREAK_K_KP6),
                                   sizeof(BREAK_K_KP7),
                                   sizeof(BREAK_K_KP8),
                                   sizeof(BREAK_K_KP9),
                                   sizeof(BREAK_K_RIGHTBRACKET),
                                   sizeof(BREAK_K_SEMICOLON),
                                   sizeof(BREAK_K_QUOTE),
                                   sizeof(BREAK_K_COMMA),
                                   sizeof(BREAK_K_PERIOD),
                                   sizeof(BREAK_K_SLASH),
                                   sizeof(BREAK_K_ACPI_POWER),
                                   sizeof(BREAK_K_ACPI_SLEEP),
                                   sizeof(BREAK_K_ACPI_WAKE),
                                   sizeof(BREAK_K_MEDIA_NEXT_TRACK),
                                   sizeof(BREAK_K_MEDIA_PREV_TRACK),
                                   sizeof(BREAK_K_MEDIA_STOP),
                                   sizeof(BREAK_K_MEDIA_PLAY_PAUSE),
                                   sizeof(BREAK_K_MEDIA_MUTE),
                                   sizeof(BREAK_K_MEDIA_VOLUME_UP),
                                   sizeof(BREAK_K_MEDIA_VOLUME_DOWN),
                                   sizeof(BREAK_K_MEDIA_MEDIA_SELECT),
                                   sizeof(BREAK_K_MEDIA_EMAIL),
                                   sizeof(BREAK_K_MEDIA_CALC),
                                   sizeof(BREAK_K_MEDIA_MY_COMPUTER),
                                   sizeof(BREAK_K_MEDIA_WWW_SEARCH),
                                   sizeof(BREAK_K_MEDIA_WWW_HOME),
                                   sizeof(BREAK_K_MEDIA_WWW_BACK),
                                   sizeof(BREAK_K_MEDIA_WWW_FORWARD),
                                   sizeof(BREAK_K_MEDIA_WWW_STOP),
                                   sizeof(BREAK_K_MEDIA_WWW_REFRESH),
                                   sizeof(BREAK_K_MEDIA_WWW_FAVORITES)};

}  // namespace scancodes

}  // namespace ps2dev

#endif /* DD85C2BD_1EA1_416E_B227_80C3C8C3E40A */