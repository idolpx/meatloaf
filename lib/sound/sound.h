//
// https://diyi0t.com/active-passive-buzzer-arduino-esp8266-esp32/
//

#ifndef SOUND_H
#define SOUND_H

#include "../../include/pinmap.h"


class SoundManager
{
public:
    SoundManager() {};
    void setup();
    void disk_spin();
    void disk_stop();
    void change_track();

};

extern SoundManager mlSoundManager;
#endif // SOUND_H