#include "PS2Commands.h"

#include <string>
#include <vector>

#include "ps2.h"
#include "ps2_keynames.h"
#include "../dos_encode.h"
#include "string_utils.h"
#include "SerialCompat.h"

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
                      "down <name>|up <name>|release|keys}\r\n");
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
