
#include "led.h"

#include "fnSystem.h"

#define BLINKING_TIME 100 // 200 ms


// Global LED manager object
LedManager fnLedManager;

LedManager::LedManager()
{
#ifdef ESP_PLATFORM
    mLedPin[eLed::LED_BUS] = PIN_LED_BUS;
    mLedPin[eLed::LED_BT] = PIN_LED_BT;
    mLedPin[eLed::LED_WIFI] = PIN_LED_WIFI;
#endif
}

// Sets required pins to OUTPUT mode and makes sure they're initially off
void LedManager::setup()
{
#ifdef ESP_PLATFORM
#if defined(PINMAP_A2_REV0) || defined(PINMAP_FUJIAPPLE_IEC) || defined(PINMAP_FUJIAPPLE_IEC_DD) || defined(PINMAP_MAC_REV0)
    fnSystem.set_pin_mode(PIN_LED_BUS, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_BUS, DIGI_LOW);

    fnSystem.set_pin_mode(PIN_LED_WIFI, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_WIFI, DIGI_HIGH);
#elif defined(PINMAP_RS232_REV0)
    fnSystem.set_pin_mode(PIN_LED_BUS, gpio_mode_t::GPIO_MODE_INPUT_OUTPUT);
    fnSystem.digital_write(PIN_LED_BUS, DIGI_HIGH);

    fnSystem.set_pin_mode(PIN_LED_WIFI, gpio_mode_t::GPIO_MODE_INPUT_OUTPUT);
    fnSystem.digital_write(PIN_LED_WIFI, DIGI_HIGH);
#else
    fnSystem.set_pin_mode(PIN_LED_BUS, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_BUS, (LEDS_INVERTED ? DIGI_HIGH : DIGI_LOW));

    fnSystem.set_pin_mode(PIN_LED_BT, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_BT, (LEDS_INVERTED ? DIGI_HIGH : DIGI_LOW));

    fnSystem.set_pin_mode(PIN_LED_WIFI, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_WIFI, (LEDS_INVERTED ? DIGI_HIGH : DIGI_LOW));
    
    fnSystem.set_pin_mode(PIN_LED_RGB_PWR, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_RGB_PWR, DIGI_HIGH);
#endif
#endif // ESP_PLATFORM
}

void LedManager::set(eLed led, bool on)
{
#ifdef ESP_PLATFORM
    if(fnSystem.ledstrip())
    {
        switch (led)
        {
        case eLed::LED_BUS:
            //oLedStrip.set(stripLed::LED_STRIP_BUS, on);
            break;
        case eLed::LED_BT:
            //oLedStrip.set(stripLed::LED_STRIP_BT, on);
            break;
        case eLed::LED_WIFI:
            //oLedStrip.set(stripLed::LED_STRIP_WIFI, on);
            break;
        default:
            break;
        }
    }
    else
    {
        mLedState[led] = on;
#if defined(PINMAP_A2_REV0) || defined(PINMAP_FUJIAPPLE_IEC) || defined(PINMAP_FUJIAPPLE_IEC_DD) || defined(PINMAP_MAC_REV0)
        // FujiApple Rev 0 BUS LED has reverse logic
        if (led == LED_BUS)
            fnSystem.digital_write(mLedPin[led], (on ? DIGI_HIGH : DIGI_LOW));
        else
            fnSystem.digital_write(mLedPin[led], (on ? DIGI_LOW : DIGI_HIGH));
#else
        on = (LEDS_INVERTED ? !on : on);
        fnSystem.digital_write(mLedPin[led], (on ? DIGI_HIGH : DIGI_LOW));
#endif
    }
#endif // ESP_PLATFORM
}

void LedManager::toggle(eLed led)
{
#ifdef ESP_PLATFORM
    if(fnSystem.ledstrip())
    {
        switch (led)
        {
        case eLed::LED_BUS:
            //oLedStrip.toggle(stripLed::LED_STRIP_BUS);
            break;
        case eLed::LED_BT:
            //oLedStrip.toggle(stripLed::LED_STRIP_BT);
            break;
        case eLed::LED_WIFI:
            //oLedStrip.toggle(stripLed::LED_STRIP_WIFI);
            break;
        default:
            break;
        }
    }
    else
    {
        set(led, !mLedState[led]);
    }
#endif // ESP_PLATFORM
}

void LedManager::blink(eLed led, int count)
{
#ifdef ESP_PLATFORM
    if(fnSystem.ledstrip())
    {
        switch (led)
        {
        case eLed::LED_BUS:
            //oLedStrip.blink(stripLed::LED_STRIP_BUS, count);
            break;
        case eLed::LED_BT:
            //oLedStrip.blink(stripLed::LED_STRIP_BT, count);
            break;
        case eLed::LED_WIFI:
            //oLedStrip.blink(stripLed::LED_STRIP_WIFI, count);
            break;
        default:
            break;
        }
    }
    else
    {
        for(int i = 0; i < count; i++)
        {
            toggle(led);
            fnSystem.delay(BLINKING_TIME);
            toggle(led);
            if(i < count - 1)
                fnSystem.delay(BLINKING_TIME);
        }
    }
#endif // ESP_PLATFORM
}
