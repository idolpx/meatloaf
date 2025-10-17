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

#include <esp_system.h>
#include <nvs_flash.h>

#ifdef BOARD_HAS_PSRAM
#include <esp_psram.h>
#ifdef CONFIG_IDF_TARGET_ESP32
#include <esp32/himem.h>
#endif
#endif

#include <driver/gpio.h>

// Disable when not debugging memory leaks
// #include "esp_heap_trace.h"
// #define NUM_RECORDS 100
// static DRAM_ATTR heap_trace_record_t trace_record[NUM_RECORDS]; // This buffer must be in internal RAM
// ESP_ERROR_CHECK( heap_trace_start(HEAP_TRACE_LEAKS) );
// DO_SOME_FUNCTION() HERE
// ESP_ERROR_CHECK( heap_trace_stop() );
// heap_trace_dump();
//
// main()
// ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );

#include "../include/global_defines.h"
#include "../include/pinmap.h"
#include "../include/debug.h"

#ifdef ENABLE_CONSOLE
#include "../lib/console/ESP32Console.h"
#endif

#ifdef ENABLE_DISPLAY
#include "display.h"
#endif

#ifdef ENABLE_BLUETOOTH
#include <c64b.h>
#endif

#ifdef ENABLE_PS2
#include "ps2_keyboard.h"
ps2dev::PS2Keyboard keyboard(PIN_KB_CLK, PIN_KB_DATA);
#endif

#include "device.h"
#include "keys.h"
#include "led.h"


#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"

#include "fsFlash.h"
#include "fnFsSD.h"



//#include "disk-sounds.h"

/**************************/
// Meatloaf


#include "bus.h"
//#include "ml_tests.h"

std::string statusMessage;
bool initFailed = false;


/**************************/


void main_shutdown_handler()
{
    Debug_println("Shutdown handler called.");
    // Give devices an opportunity to clean up before rebooting

    IEC.shutdown();
}

// Initial setup
void main_setup()
{
    // ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );

    unsigned long startms = fnSystem.millis();

#ifdef DELAY_START_MILLIS
    vTaskDelay(DELAY_START_MILLIS / portTICK_PERIOD_MS);
#endif

#ifdef ENABLE_CONSOLE
    //You can change the console prompt before calling begin(). By default it is "ESP32>"
    console.setPrompt("meatloaf[%pwd%]# ");

    //You can change the baud rate and pin numbers similar to Serial.begin() here.
    console.begin(DEBUG_SPEED);
#else
    Serial.begin(DEBUG_SPEED);
#endif

    printf( ANSI_WHITE "\r\n\r\n" ANSI_BLUE_BACKGROUND "==============================" ANSI_RESET_NL );
    printf( ANSI_BLUE_BACKGROUND "   " PRODUCT_ID " " FW_VERSION "   " ANSI_RESET_NL );
    printf( ANSI_BLUE_BACKGROUND "   " PLATFORM_DETAILS "    " ANSI_RESET_NL );
    printf( ANSI_BLUE_BACKGROUND "------------------------------" ANSI_RESET_NL "\r\n" );

    //printf( "Meatloaf %s Started @ %lu\r\n", fnSystem.get_fujinet_version(), startms );

    printf( "RAM  : %lu\r\n", fnSystem.get_free_heap_size() );

#ifdef BOARD_HAS_PSRAM
    //printf( "PSRAM: %lu\r\n", fnSystem.get_psram_size() );
#ifdef CONFIG_IDF_TARGET_ESP32
    printf( "HIMEM: %u\r\n", esp_himem_get_phys_size() );
    //printf( "HIMEM free %u\r\n", esp_himem_get_free_size() );
    //printf( "HIMEM reserved %u\r\n", esp_himem_reserved_area_size() );
#endif
#endif

    // Install a reboot handler
    esp_register_shutdown_handler(main_shutdown_handler);

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        printf("Erasing flash");
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);

    fnSystem.check_hardware_ver();
    printf("Detected Hardware Version: %s\r\n", fnSystem.get_hardware_ver_str());

    // Setup hardware
    fnKeyManager.setup();
    fnLedManager.setup();

#ifndef MIN_CONFIG
    if (PIN_MODEM_ENABLE != GPIO_NUM_NC && PIN_MODEM_UP9600 != GPIO_NUM_NC) {
        // Enable/Disable Modem/Parallel Mode on Userport
        fnSystem.set_pin_mode(PIN_MODEM_ENABLE, gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(PIN_MODEM_ENABLE, DIGI_LOW); // DISABLE Modem
        //fnSystem.digital_write(PIN_MODEM_ENABLE, DIGI_HIGH); // ENABLE Modem
        fnSystem.set_pin_mode(PIN_MODEM_UP9600, gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(PIN_MODEM_UP9600, DIGI_LOW); // DISABLE UP9600
        //fnSystem.digital_write(PIN_MODEM_UP9600, DIGI_HIGH); // ENABLE UP9600
    }
#endif

    // Initialize the FileSystem
    printf("Initializing FileSystem\r\n");
    if ( fsFlash.start() )
    {
        // Create SYSTEM DIR if it doesn't exist
        fsFlash.create_path( SYSTEM_DIR );
    }

#ifdef SD_CARD
    if ( fnSDFAT.start() )
    {
        // Create SYSTEM DIR if it doesn't exist
        fnSDFAT.create_path( SYSTEM_DIR );
    }
#endif

    // setup crypto key - must be done before loading the config
    crypto.setkey("MLK" + fnWiFi.get_mac_str());

    // Load our stored configuration
    Config.load();

    // Setup IEC Bus
    IEC.setup();
    printf(ANSI_GREEN_BOLD "IEC Bus Initialized" ANSI_RESET "\r\n");

    Meatloaf.setup(&IEC);
    // {
    //     // Add devices to bus
    //     FileSystem *ptrfs = fnSDFAT.running() ? (FileSystem *)&fnSDFAT : (FileSystem *)&fsFlash;
    //     iecPrinter::printer_type ptype = iecPrinter::printer_type::PRINTER_COMMODORE_MPS803; // temporary
    //     printf("Creating a default printer using %s storage and type %d\r\n", ptrfs->typestring(), ptype);
    //     iecPrinter *ptr = new iecPrinter(ptrfs, ptype);
    //     fnPrinters.set_entry(0, ptr, ptype, Config.get_printer_port(0));

    //     printf("Printer "); IEC.addDevice(ptr, 4);                    // 04-07 Printers / Plotters
    //     printf("Disk "); IEC.addDevice(new iecDrive(), 8);            // 08-15 Drives
    //     printf("Network "); IEC.addDevice(new iecNetwork(), 16);      // 16-19 Network Devices
    //     printf("CPM "); IEC.addDevice(new iecCpm(), 20);              // 20-29 Other
    //     printf("Voice "); IEC.addDevice(new iecVoice(), 21);
    //     printf("Clock "); IEC.addDevice(new iecClock(), 28);
    //     printf("OpenAI "); IEC.addDevice(new iecOpenAI(), 29);
    //     printf("Meatloaf "); IEC.addDevice(new iecMeatloaf(), 30);    // 30    Meatloaf

    //     printf("Virtual Device(s) Started: [ " ANSI_YELLOW_BOLD );
    //     for (uint8_t i = 0; i < 31; i++)
    //     {
    //         if (IEC.isDeviceEnabled(i))
    //         {
    //             printf("%.02d ", i);
    //         }
    //     }
    //     printf( ANSI_RESET "]\r\n");
    //     //IEC.enabled = true;
    // }

#ifdef PARALLEL_BUS_X
    // Setup Parallel Bus
    PARALLEL.setup();
    printf( ANSI_GREEN_BOLD "Parallel Bus Initialized" ANSI_RESET "\r\n" );
#endif

#ifdef ENABLE_DISPLAY
    LEDS.start();
    //LEDS.show_image( (char *)WWW_ROOT "/assets/logo.png" );
    LCD.show_image( (char *)WWW_ROOT "/assets/logo.l.png" );
#endif

#ifdef ENABLE_AUDIO
    AUDIO.start(); // start sound
#endif

#ifdef DEBUG
    unsigned long endms = fnSystem.millis();
    Debug_printf("Available heap: %lu\r\nSetup complete @ %lu (%lums)\r\n", fnSystem.get_free_heap_size(), endms, endms - startms);
#endif // DEBUG


    // Set up the WiFi adapter
    fnWiFi.start();

#ifdef DEBUG_TIMING
    Debug_printv( ANSI_GREEN_BOLD "DEBUG_TIMING enabled" ANSI_RESET );
#endif

//#ifdef RUN_TESTS
//    runTestsSuite();
    // lfs_test();
//#endif

#ifdef ENABLE_CONSOLE
    //Register builtin commands like 'reboot', 'version', or 'meminfo'
    console.registerSystemCommands();

    //Register network commands
    console.registerNetworkCommands();

    //Register the VFS specific commands
    console.registerVFSCommands();

    //Register GPIO commands
    console.registerGPIOCommands();

    //Register XFER commands
    console.registerXFERCommands();
#endif

    printf("READY.\r\n");
}

/*
 * This is the start/entry point for an ESP-IDF program (must use "C" linkage)
 */
extern "C"
{
    void app_main()
    {
        // Call our setup routine
        main_setup();

#ifdef ENABLE_BLUETOOTH
        // Setup Bluetooth
        bt_setup();
#endif

        // Delete app_main() task since we no longer need it
        vTaskDelete(NULL);
    }
}
