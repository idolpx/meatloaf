#ifndef LED_STRIP_H
#define LED_STIP_H
#ifdef LED_STRIP

#include "FastLED.h"
//#include "FX.h"

#include "../../include/pinmap.h"

#define LED_BLINK_TIME 100

// These must be defined for compile even if not used
#ifndef LED_STRIP
/* LED Strip NEW */
#define NUM_LEDS            5
#define LED_DATA_PIN        4
#define LED_BRIGHTNESS      15 // max mA the LED can use determines brightness
#define LED_TYPE            WS2812B
#define RGB_ORDER           GRB
// LED order on the strip starting with 0
#define LED_WIFI_NUM        0
#define LED_BUS_NUM         4
#define LED_BT_NUM          2
#endif

enum stripLed
{
  LED_STRIP_WIFI = LEDSTRIP_WIFI_NUM,
  LED_STRIP_BUS = LEDSTRIP_BUS_NUM,
  LED_STRIP_BT = LEDSTRIP_BT_NUM,
  LED_STRIP_COUNT = LEDSTRIP_COUNT
};

enum ledState
{
  LED_OFF = 0,
  LED_ON = 1,
  LED_BLINK = 2
};

// Task that always runs to control led strip
void ledStripTask(void *pvParameters);

// Taste the Rainbow
void rainbow_wave(uint8_t thisSpeed, uint8_t deltaHue);

class LedStrip
{
public:
  int sLedState[LEDSTRIP_COUNT] = { LED_OFF }; // default off
  CRGB sLedColor[LEDSTRIP_COUNT] = { CRGB::Black }; // default black (off)
  int sLedBlinkCount[LEDSTRIP_COUNT] = { 0 }; // default no blinky
  int rainbowTimer = 0; // run the rainbow animation for this many seconds
  bool rainbowStopper = false; // flag the loop to stop if true

  LedStrip();
  void setup();
  void set(stripLed led, bool onoff);
  void setColor(int led, CRGB color);
  void toggle(stripLed led);
  void blink(stripLed led, int count=1);
  void startRainbow(int seconds);
  void stopRainbow();

private:
};

extern LedStrip oLedStrip;

#endif // LED_STRIP
#endif // LED_STRIP_H