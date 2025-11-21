// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#ifdef BUILD_GPIB

#include "gpib.h"
#include "../../device/iec/meatloaf.h"

#include <cstring>
#include <memory>

#include "soc/io_mux_reg.h"
#include "driver/gpio.h"
#include "hal/gpio_hal.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"
#include "../../hardware/led.h"

#define MAIN_STACKSIZE	 32768
#define MAIN_PRIORITY	 17
#define MAIN_CPUAFFINITY 1

systemBus GPIB;

systemBus::systemBus() : 
  GPIBusHandler(PIN_GPIB_ATN, 
                PIN_GPIB_DAV, PIN_GPIB_NRFD, PIN_GPIB_NDAC, PIN_GPIB_EOI,
                PIN_GPIB_IFC==GPIO_NUM_NC ? 0xFF : PIN_GPIB_IFC,
                0xFF,
                PIN_GPIB_SRQ==GPIO_NUM_NC   ? 0xFF : PIN_GPIB_SRQ)
{
  setParallelPins(PIN_PARALLEL_DATA0,
                  PIN_PARALLEL_DATA1,
                  PIN_PARALLEL_DATA2,
                  PIN_PARALLEL_DATA3,
                  PIN_PARALLEL_DATA4,
                  PIN_PARALLEL_DATA5,
                  PIN_PARALLEL_DATA6,
                  PIN_PARALLEL_DATA7);
}

static void ml_gpib_intr_task(void* arg)
{
    while ( true )
    {
      GPIB.service();
      taskYIELD(); // Allow other tasks to run
    }
}

// void init_gpio(gpio_num_t _pin)
// {
//     PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[_pin], PIN_FUNC_GPIO);
//     gpio_set_direction(_pin, GPIO_MODE_INPUT);
//     gpio_pullup_en(_pin);
//     gpio_set_pull_mode(_pin, GPIO_PULLUP_ONLY);
//     gpio_set_level(_pin, 0);
//     return;
// }

void systemBus::setup()
{
  Debug_printf("GPIB systemBus::setup()\r\n");
  begin();

//     // initial pin modes in GPIO
//     init_gpio(PIN_GPIB_ATN);
//     init_gpio(PIN_GPIB_DAV);
//     init_gpio(PIN_GPIB_EOI);
//     init_gpio(PIN_GPIB_DATA_IN);
//     init_gpio(PIN_GPIB_DATA_OUT);
//     init_gpio(PIN_GPIB_SRQ);
// #ifdef GPIB_HAS_RESET
//     init_gpio(PIN_GPIB_IFC);
// #endif

#ifdef GPIB_INVERTED_LINES
#warning intr_type likely needs to be fixed!
#endif

    // Start task
    // Create a new high-priority task to handle the main service loop
    // This is assigned to CPU1; the WiFi task ends up on CPU0
    xTaskCreatePinnedToCore(ml_gpib_intr_task, "bus_gpib", MAIN_STACKSIZE, NULL, MAIN_PRIORITY, NULL, MAIN_CPUAFFINITY);

}


void systemBus::service()
{
  task();
  
  bool error = false, active = false;
  for(int i = 0; i < MAX_DISK_DEVICES; i++)
    {
      iecDrive *d = &(Meatloaf.get_disks(i)->disk_dev);
      error  |= d->hasError();
      active |= d->getNumOpenChannels()>0;
    }

  if( error )
    {
      static bool     flashState = false;
      static uint32_t prevFlash  = 0;
      if( (fnSystem.millis()-prevFlash) > 250 )
        {
          flashState = !flashState;
          fnLedManager.set(eLed::LED_BUS, flashState);
          prevFlash = fnSystem.millis();
        }
    }
  else
    fnLedManager.set(eLed::LED_BUS, active);
}


void systemBus::shutdown()
{
  printf("GPIB systemBus::shutdown()\r\n");
}


#endif /* BUILD_GPIB */
