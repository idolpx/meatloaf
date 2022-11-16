
#include "sound.h"

#include "../include/pinmap.h"
#include "fnSystem.h"

SoundManager mlSoundManager;

void SoundManager::setup ( void )
{
    fnSystem.set_pin_mode ( PIN_PIEZO, GPIO_MODE_OUTPUT );
    fnSystem.digital_write ( PIN_PIEZO, 0 );
}

void disk_spin() {};
void disk_stop() {};
void change_track() {};

