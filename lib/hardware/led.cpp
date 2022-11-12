#include "led.h"

#include "fnSystem.h"


#define BLINKING_TIME 100 // 200 ms


// Global LED manager object
LedManager fnLedManager;

LedManager::LedManager()
{
    mLedPin[eLed::LED_BUS] = PIN_LED_BUS;
    mLedPin[eLed::LED_BT] = PIN_LED_BT;
    mLedPin[eLed::LED_WIFI] = PIN_LED_WIFI;
}

// Sets required pins to OUTPUT mode and makes sure they're initially off
void LedManager::setup()
{
#ifndef FUJILOAF_REV0
    fnSystem.set_pin_mode(PIN_LED_BUS, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_BUS, DIGI_HIGH);
 
    fnSystem.set_pin_mode(PIN_LED_BT, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_BT, DIGI_HIGH);    

    fnSystem.set_pin_mode(PIN_LED_WIFI, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_WIFI, DIGI_HIGH);
#endif
}

void LedManager::set(eLed led, bool on)
{
#ifndef FUJILOAF_REV0
    mLedState[led] = on;
    fnSystem.digital_write(mLedPin[led], (on ? DIGI_LOW : DIGI_HIGH));
#endif
}

void LedManager::toggle(eLed led)
{
#ifndef FUJILOAF_REV0
    set(led, !mLedState[led]);
#endif
}

void LedManager::blink(eLed led, int count)
{
#ifndef FUJILOAF_REV0
    for(int i = 0; i < count; i++)
    {
        toggle(led);
        fnSystem.delay(BLINKING_TIME);
        toggle(led);
        if(i < count - 1)
            fnSystem.delay(BLINKING_TIME);
    }
#endif
}
