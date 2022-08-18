

#ifndef SOUND_H
#define SOUND_H

#include "../../include/pinmap.h"

enum eLed
{
    LED_WIFI = 0,
    LED_BUS,
    LED_BT,
    LED_COUNT
};

class SoundManager
{
public:
    SoundManager();
    void setup();
    void set(eLed led, bool one=true);
    void toggle(eLed led);
    void blink(eLed led, int count=1);

};

extern SoundManager mlSoundManager;
#endif // SOUND_H