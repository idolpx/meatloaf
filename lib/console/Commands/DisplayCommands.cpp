#include "DisplayCommands.h"

#ifdef ENABLE_DISPLAY

#include "display.h"
#include "string_utils.h"

static int led(int argc, char **argv)
{
    if (argc < 2) {
        Serial.printf("led {idle|send|receive|activity|progress {0-100}|status {1-255}|speed {0-255}|brightness {*|index} {0-255}}\r\n");
        return EXIT_FAILURE;
    }

    if (mstr::startsWith(argv[1], "idle"))
    {
        LEDS.idle();
    }
    else if (mstr::startsWith(argv[1], "send"))
    {
        LEDS.send();
    }
    else if (mstr::startsWith(argv[1], "receive"))
    {
        LEDS.receive();
    }
    else if (mstr::startsWith(argv[1], "activity"))
    {
        LEDS.activity = !LEDS.activity;
    }
    else if (mstr::startsWith(argv[1], "progress"))
    {
        if (argc == 3)
            LEDS.progress = atoi(argv[2]);
        else
            LEDS.idle();
    }
    else if (mstr::startsWith(argv[1], "status"))
    {
        if (argc == 3)
            LEDS.status(atoi(argv[2]));
        else
            LEDS.idle();
    }
    else if (mstr::startsWith(argv[1], "speed"))
    {
        if (argc == 3)
            LEDS.speed = atoi(argv[2]);
        else
            LEDS.idle();
    }
    else if (mstr::startsWith(argv[1], "pixel"))
    {
        int i = -1;
        if (mstr::isNumeric(argv[2]))
            i = atoi(argv[2]);

        // if (argc == 6)
        //     uint16_t index = atoi(argv[2]);
        //     uint8_t r = atoi(argv[3]);
        //     uint8_t g = atoi(argv[4]);
        //     uint8_t b = atoi(argv[5]);
        //     if (i == -1) // all
        //         LEDS.fill_all((CRGB){.r=r, .g=g, .b=b});
        //     else
        //         LEDS.set_pixel(index, r, g, b);
        // else
        //     LEDS.idle();
    }
    else if (mstr::startsWith(argv[1], "brightness"))
    {
        if (argc == 3)
        {
            LEDS.set_brightness(static_cast<uint8_t>(atoi(argv[2])));
        }
        else if (argc == 4)
        {
            uint8_t brightness = static_cast<uint8_t>(atoi(argv[3]));
            if (strcmp(argv[2], "*") == 0)
                LEDS.set_all_pixel_brightness(brightness);
            else
                LEDS.set_pixel_brightness(static_cast<uint16_t>(atoi(argv[2])), brightness);
        }
        else
        {
            Serial.printf("led brightness [*|index] {0-255}\r\n");
            return EXIT_FAILURE;
        }
    }
    else
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

namespace ESP32Console::Commands
{
    const ConsoleCommand getLEDCommand()
    {
        return ConsoleCommand("led", &led, "Change LED display settings");
    }
}

#endif
