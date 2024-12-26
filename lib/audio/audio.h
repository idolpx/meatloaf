//
// https://diyi0t.com/active-passive-buzzer-arduino-esp8266-esp32/
//

#ifndef AUDIO_H
#define AUDIO_H

#include "freertos/FreeRTOS.h"
#include <freertos/queue.h>
#include "freertos/task.h"

#include "../../include/pinmap.h"


class Audio
{
public:
    Audio() {};

    void spin();
    void stop();
    void change_track();

};

extern Audio audio;
#endif // AUDIO_H