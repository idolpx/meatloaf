// PS/2 keyboard output device.
//
// Guarded on PIN_KB_CLK, the way lib/display/lcd.h guards on PIN_TFT_MOSI:
// a board that wires the pins has the hardware, and that is the whole
// condition.  Boards without it get a no-op class with identical signatures,
// so call sites need no #ifdef of their own.
#ifndef DEVICE_PS2_H
#define DEVICE_PS2_H

#include <string>
#include <vector>

// MUST come before the PIN_KB_CLK check below.  Without it, a TU that has
// not already included the pinmap sees the stub branch while one that has
// sees the real class -- an ODR violation that surfaces as undefined
// references to PS2KeyboardDevice::start()/reloadConfig() at link time.
// Same pattern as lib/display/lcd.h.
#include "../../include/pinmap.h"

#include "ps2_keynames.h"

#ifdef PIN_KB_CLK

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ps2_keyboard.h"

class PS2KeyboardDevice
{
public:
    PS2KeyboardDevice();

    void start();                 // boot: reads config only, allocates nothing
    bool isEnabled() const { return _enabled; }
    bool isRunning() const { return _started; }

    bool startDevice();           // lazy begin(); re-announces BAT if already up
    bool enable();
    bool disable();               // releaseAll() -> end() -> persist; frees ~8 KB
    void persistConfig();
    void reloadConfig();

    bool typeText(const std::string &text);
    bool holdKey(ps2keys::Key k);
    bool releaseKey(ps2keys::Key k);
    // No single-key pressKey(): `ps2 key <name>` goes through parseCombo,
    // which yields a one-element vector, so pressCombo covers both.
    bool pressCombo(const std::vector<ps2keys::Key> &keys);
    void releaseAll();

    const std::vector<ps2keys::Key> &heldKeys() const { return _held; }
    const ps2keys::Overrides &overrides() const { return _overrides; }
    bool dataReportingEnabled();
    bool capsLockOn();
    bool numLockOn();
    bool scrollLockOn();

private:
    bool ensureStarted();
    void releaseAllLocked();      // caller already holds _api_mutex

    bool _enabled = false;
    bool _started = false;
    std::vector<ps2keys::Key> _held;
    ps2keys::Overrides _overrides;
    SemaphoreHandle_t _api_mutex;
    ps2dev::PS2Keyboard _kb;
};

#else   // no PS/2 hardware on this board

class PS2KeyboardDevice
{
public:
    void start() {}
    bool isEnabled() const { return false; }
    bool isRunning() const { return false; }
    bool startDevice() { return false; }
    bool enable() { return false; }
    bool disable() { return false; }
    void persistConfig() {}
    void reloadConfig() {}
    bool typeText(const std::string &) { return false; }
    bool holdKey(ps2keys::Key) { return false; }
    bool releaseKey(ps2keys::Key) { return false; }
    bool pressCombo(const std::vector<ps2keys::Key> &) { return false; }
    void releaseAll() {}
    const std::vector<ps2keys::Key> &heldKeys() const
        { static std::vector<ps2keys::Key> empty; return empty; }
    const ps2keys::Overrides &overrides() const
        { static ps2keys::Overrides empty; return empty; }
    bool dataReportingEnabled() { return false; }
    bool capsLockOn() { return false; }
    bool numLockOn() { return false; }
    bool scrollLockOn() { return false; }
};

#endif // PIN_KB_CLK

extern PS2KeyboardDevice ps2Keyboard;

#endif // DEVICE_PS2_H
