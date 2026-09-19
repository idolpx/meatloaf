#include "PS2Commands.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "ps2.h"
#include "ps2_keynames.h"
#include "../dos_encode.h"
#include "string_utils.h"
#include "SerialCompat.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using ESP32Console::encodeAsciiCommand;

namespace
{
    // The console splitter gives each word its own argv entry and collapses
    // runs of whitespace, so a typed sentence has to be rejoined.  Same two
    // limitations `write` has: consecutive spaces cannot be reproduced, and
    // esp_console_run() silently drops arguments past
    // CONSOLE_MAX_CMDLINE_ARGS (32).
    std::string joinFrom(int argc, char **argv, int first)
    {
        std::string out;
        for (int i = first; i < argc; i++)
        {
            if (!out.empty()) out += ' ';
            out += argv[i];
        }
        return out;
    }

    void printStatus()
    {
        Serial.printf("ps2: enabled[%d] running[%d]\r\n",
                      ps2Keyboard.isEnabled() ? 1 : 0,
                      ps2Keyboard.isRunning() ? 1 : 0);

        if (!ps2Keyboard.isRunning())
        {
            Serial.printf("     host handshake: n/a (device not started)\r\n");
            return;
        }

        // If the host booted before `ps2 start` ran, it will never have sent
        // anything -- this line is what names that, rather than leaving you
        // typing into the void.
        Serial.printf("     data reporting[%d]  caps[%d] num[%d] scroll[%d]\r\n",
                      ps2Keyboard.dataReportingEnabled() ? 1 : 0,
                      ps2Keyboard.capsLockOn() ? 1 : 0,
                      ps2Keyboard.numLockOn() ? 1 : 0,
                      ps2Keyboard.scrollLockOn() ? 1 : 0);

        const std::vector<ps2keys::Key> &held = ps2Keyboard.heldKeys();
        Serial.printf("     held[%d]", (int)held.size());
        for (size_t i = 0; i < held.size(); i++)
        {
            const char *n = ps2keys::keyName(held[i]);
            Serial.printf(" %s", n ? n : "?");
        }
        Serial.printf("\r\n");
    }

    int ps2(int argc, char **argv)
    {
        if (argc < 2 || std::string(argv[1]) == "status")
        {
            printStatus();
            return EXIT_SUCCESS;
        }

        std::string sub = argv[1];

        if (sub == "start")
        {
            bool ok = ps2Keyboard.startDevice();
            if (!ok)
                Serial.printf("ps2: start failed (disabled? run `ps2 enable`)\r\n");
            printStatus();
            return ok ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        if (sub == "enable")  { ps2Keyboard.enable();  printStatus(); return EXIT_SUCCESS; }
        if (sub == "disable") { ps2Keyboard.disable(); printStatus(); return EXIT_SUCCESS; }
        if (sub == "release") { ps2Keyboard.releaseAll(); return EXIT_SUCCESS; }

        if (sub == "speed")
        {
#ifdef PIN_KB_CLK
            // Spec-legal half-period is 30-50us (38 default). A host that
            // can't keep up with spec-legal timing -- e.g. a soft-polled
            // receiver rather than an interrupt-driven one -- may need this
            // slower to be read at all.
            if (argc < 3)
            {
                Serial.printf("ps2: clock half-period is %u us (default 38, spec range 30-50)\r\n",
                              (unsigned)ps2dev::CLK_HALF_PERIOD_MICROS);
                return EXIT_SUCCESS;
            }
            uint32_t v = (uint32_t)strtoul(argv[2], nullptr, 10);
            if (v == 0)
            {
                Serial.printf("usage: ps2 speed <half-period-us>\r\n");
                return EXIT_FAILURE;
            }
            ps2dev::set_clk_half_period_us(v);
            Serial.printf("ps2: clock half-period now %u us (~%.1f kHz)\r\n",
                          (unsigned)v, 500.0 / v);
#else
            Serial.printf("ps2: not supported on this board\r\n");
#endif
            return EXIT_SUCCESS;
        }

        if (sub == "keys")
        {
            std::vector<const char *> names;
            ps2keys::allNames(names);
            for (size_t i = 0; i < names.size(); i++)
                Serial.printf("%s%s", names[i], ((i % 8) == 7) ? "\r\n" : " ");
            Serial.printf("\r\n");
            return EXIT_SUCCESS;
        }

        if (sub == "type")
        {
            if (argc < 3) { Serial.printf("usage: ps2 type <text>\r\n"); return EXIT_FAILURE; }
            // encodeAsciiCommand applies the 0xNN run escape WITHOUT PETSCII,
            // so `ps2 type "dir0x0D"` sends dir then Enter.
            std::string text = encodeAsciiCommand(joinFrom(argc, argv, 2));
            if (!ps2Keyboard.typeText(text))
            {
                Serial.printf("ps2: type failed (disabled, not started, or non-ASCII)\r\n");
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        }

        if (sub == "wiggle")
        {
#ifdef PIN_KB_CLK
            // Raw GPIO toggling, bypassing PS2Device entirely -- lets CLK/DATA
            // be confirmed with a plain multimeter when no scope/logic
            // analyzer is available. Must not race the background tasks, so
            // it refuses while they're still holding the pins.
            if (ps2Keyboard.isRunning())
            {
                Serial.printf("ps2: run `ps2 disable` first -- wiggle drives "
                              "CLK/DATA directly and must not race the running device\r\n");
                return EXIT_FAILURE;
            }

            gpio_config_t io_conf = {};
            io_conf.mode = GPIO_MODE_OUTPUT_OD;
            io_conf.pin_bit_mask = (1ULL << PIN_KB_CLK) | (1ULL << PIN_KB_DATA);
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            gpio_config(&io_conf);
            gpio_set_level(PIN_KB_CLK, 1);
            gpio_set_level(PIN_KB_DATA, 1);

            Serial.printf("ps2: wiggle -- probe CLK[%d] and DATA[%d] with a meter now\r\n",
                          (int)PIN_KB_CLK, (int)PIN_KB_DATA);
            for (int i = 0; i < 5; i++)
            {
                Serial.printf("  [%d] CLK low  / DATA high\r\n", i);
                gpio_set_level(PIN_KB_CLK, 0);
                gpio_set_level(PIN_KB_DATA, 1);
                vTaskDelay(pdMS_TO_TICKS(1500));

                Serial.printf("  [%d] CLK high / DATA low\r\n", i);
                gpio_set_level(PIN_KB_CLK, 1);
                gpio_set_level(PIN_KB_DATA, 0);
                vTaskDelay(pdMS_TO_TICKS(1500));
            }
            gpio_set_level(PIN_KB_CLK, 1);
            gpio_set_level(PIN_KB_DATA, 1);
            Serial.printf("ps2: wiggle done -- run `ps2 enable` (or `ps2 start`) to resume\r\n");
#else
            Serial.printf("ps2: not supported on this board\r\n");
#endif
            return EXIT_SUCCESS;
        }

        if (sub == "key" || sub == "down" || sub == "up")
        {
            if (argc < 3)
            {
                Serial.printf("usage: ps2 %s <name>\r\n", sub.c_str());
                return EXIT_FAILURE;
            }

            if (sub == "key")
            {
                std::vector<ps2keys::Key> keys;
                if (!ps2keys::parseCombo(argv[2], keys, &ps2Keyboard.overrides()))
                {
                    Serial.printf("ps2: unknown key in '%s' (try `ps2 keys`)\r\n", argv[2]);
                    return EXIT_FAILURE;
                }
                return ps2Keyboard.pressCombo(keys) ? EXIT_SUCCESS : EXIT_FAILURE;
            }

            ps2keys::Key k;
            if (!ps2keys::lookupKey(argv[2], k, &ps2Keyboard.overrides()))
            {
                Serial.printf("ps2: unknown key '%s' (try `ps2 keys`)\r\n", argv[2]);
                return EXIT_FAILURE;
            }
            bool ok = (sub == "down") ? ps2Keyboard.holdKey(k) : ps2Keyboard.releaseKey(k);
            return ok ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        Serial.printf("ps2 {status|start|enable|disable|type <text>|key <a>[+<b>]|"
                      "down <name>|up <name>|release|keys|wiggle|speed [us]}\r\n");
        return EXIT_FAILURE;
    }
}

namespace ESP32Console::Commands
{
    const ConsoleCommand getPS2Command()
    {
        return ConsoleCommand("ps2", &ps2, "Send keystrokes over the PS/2 interface");
    }
}
