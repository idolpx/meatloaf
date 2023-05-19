
#include "disk-sounds.h"

#include "../../include/pinmap.h"

#include "fnSystem.h"

diskSounds mlDiskSounds;

void diskSounds::setup ( void )
{
    //fnSystem.set_pin_mode ( PIN_DAC1, GPIO_MODE_OUTPUT );
    //fnSystem.digital_write ( PIN_DAC1, 0 );
}

void spin() {};
void stop() {};
void change_track() {};

