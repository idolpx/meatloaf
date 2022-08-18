
#include "sound.h"

#include "../include/pinmap.h"
#include "fnSystem.h"

void SoundManager::setup ( void )
{
    fnSystem.set_pin_mode ( PIN_IEC_ATN, GPIO_MODE_OUTPUT );
    fnSystem.digital_write ( PIN_PIEZO, 0 )
}
