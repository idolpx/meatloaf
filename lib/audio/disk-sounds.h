//
// https://diyi0t.com/active-passive-buzzer-arduino-esp8266-esp32/
//

#ifndef DISK_SOUNDS_H
#define DISK_SOUNDS_H

#include "../../include/pinmap.h"


class diskSounds
{
public:
    diskSounds() {};

    void spin();
    void stop();
    void change_track();

};

extern diskSounds mlDiskSounds;
#endif // DISK_SOUNDS_H