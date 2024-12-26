#ifdef ENABLE_AUDIO

#include "audio.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include <freertos/queue.h>
#include "freertos/task.h"

#include "../../include/pinmap.h"
#include "../../include/debug.h"

#include "../../include/pinmap.h"

Audio audio;

void Audio::setup ( void )
{
    //fnSystem.set_pin_mode ( PIN_DAC1, GPIO_MODE_OUTPUT );
    //fnSystem.digital_write ( PIN_DAC1, 0 );
}

void spin() {};
void stop() {};
void change_track() {};

#endif // ENABLE_AUDIO
