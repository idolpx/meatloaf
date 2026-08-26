#include "ps2.h"

#include "mlConfig.h"
#include "debug.h"

PS2KeyboardDevice ps2Keyboard;

#ifdef PIN_KB_CLK

PS2KeyboardDevice::PS2KeyboardDevice()
    : _api_mutex(xSemaphoreCreateMutex()), _kb(PIN_KB_CLK, PIN_KB_DATA)
{
}

// Boot: read config and nothing else.  Allocating here would defeat the
// point of the lazy start -- the two tasks cost ~8 KB of INTERNAL DRAM,
// which is the scarce kind on this platform.
void PS2KeyboardDevice::start()
{
    reloadConfig();
}

void PS2KeyboardDevice::reloadConfig()
{
    const psram_json &devices = mlConfig["devices"];
    if (!devices.contains("ps2"))
        return;

    const psram_json &ps2 = devices["ps2"];
    bool want = ps2.value("enabled", 0) != 0;

    // The DTV's scancode-to-C64-key table is a bench finding, so C64 names
    // (runstop, restore, commodore...) are bound here from config rather
    // than guessed in the built-in table.
    _overrides.clear();
    if (ps2.contains("keymap"))
    {
        const psram_json &km = ps2["keymap"];
        for (auto it = km.begin(); it != km.end(); ++it)
            if (it.value().is_string())
                _overrides[it.key()] = it.value().get<std::string>();
    }

    if (!want && _started)
        disable();          // takes _api_mutex itself; not held here
    _enabled = want;
}

void PS2KeyboardDevice::persistConfig()
{
    auto &entry = mlConfig.data()["devices"]["ps2"];
    entry["enabled"] = _enabled ? 1 : 0;
}

bool PS2KeyboardDevice::ensureStarted()
{
    if (!_enabled)
        return false;
    if (_started)
        return true;

    _kb.begin();          // two tasks, 200 ms settle, 0xAA BAT announcement
    _started = true;
    Debug_printv("ps2: started on clk[%d] data[%d]", (int)PIN_KB_CLK, (int)PIN_KB_DATA);
    return true;
}

// `ps2 start` on an already-running device RE-ANNOUNCES rather than
// no-opping.  A host that booted before we did generally never re-probes,
// so this is the best-effort hot-plug hook.
bool PS2KeyboardDevice::startDevice()
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    bool ok;
    if (_started)
    {
        ok = (_kb.write_wait_idle(0xAA) == 0);
        Debug_printv("ps2: re-announced BAT, ok[%d]", ok ? 1 : 0);
    }
    else
    {
        ok = ensureStarted();
    }
    xSemaphoreGive(_api_mutex);
    return ok;
}

bool PS2KeyboardDevice::enable()
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    _enabled = true;
    persistConfig();
    xSemaphoreGive(_api_mutex);
    mlConfig.save();      // outside the mutex: this writes flash
    return true;
}

bool PS2KeyboardDevice::disable()
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    _enabled = false;          // no new sends may start
    if (_started)
    {
        releaseAllLocked();    // never leave the host holding a modifier
        _kb.end();             // reclaims the two task stacks, queue and mutex
        _started = false;
    }
    persistConfig();
    xSemaphoreGive(_api_mutex);
    mlConfig.save();
    return true;
}

bool PS2KeyboardDevice::typeText(const std::string &text)
{
    // ASCII only.  A non-ASCII character has no PS/2 scancode, and this path
    // must NEVER be PETSCII-encoded -- PS/2 goes to a keyboard controller,
    // not the IEC bus.  The DTV still DISPLAYS PETSCII, but that mapping
    // happens on the DTV side from the scancodes we send.
    for (size_t i = 0; i < text.size(); i++)
        if ((unsigned char)text[i] > 0x7F)
        {
            Debug_printv("ps2: non-ASCII byte [%02X] has no scancode", (unsigned char)text[i]);
            return false;
        }

    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    bool ok = ensureStarted() && (_kb.type(text.c_str()) == 0);
    xSemaphoreGive(_api_mutex);
    return ok;
}

bool PS2KeyboardDevice::holdKey(ps2keys::Key k)
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    bool ok = ensureStarted() && (_kb.keydown(k) == 0);
    if (ok)
    {
        bool already = false;
        for (size_t i = 0; i < _held.size(); i++)
            if (_held[i] == k) already = true;
        if (!already)
            _held.push_back(k);
    }
    xSemaphoreGive(_api_mutex);
    return ok;
}

bool PS2KeyboardDevice::releaseKey(ps2keys::Key k)
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    bool ok = ensureStarted() && (_kb.keyup(k) == 0);
    for (size_t i = 0; i < _held.size(); i++)
        if (_held[i] == k) { _held.erase(_held.begin() + i); break; }
    xSemaphoreGive(_api_mutex);
    return ok;
}

bool PS2KeyboardDevice::pressCombo(const std::vector<ps2keys::Key> &keys)
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    bool ok = ensureStarted();
    if (ok)
    {
        for (size_t i = 0; i < keys.size(); i++)
            if (_kb.keydown(keys[i]) != 0) ok = false;
        // Released in REVERSE so modifiers outlive the key they modify.
        for (size_t i = keys.size(); i > 0; i--)
            if (_kb.keyup(keys[i - 1]) != 0) ok = false;
    }
    xSemaphoreGive(_api_mutex);
    return ok;
}

// Leaving Ctrl or Shift down modifies every keystroke typed on the HOST's
// own keyboard, so a dropped connection could wedge the target machine.
void PS2KeyboardDevice::releaseAllLocked()
{
    if (!_started)
    {
        _held.clear();
        return;
    }
    for (size_t i = _held.size(); i > 0; i--)
        _kb.keyup(_held[i - 1]);
    _held.clear();
}

void PS2KeyboardDevice::releaseAll()
{
    xSemaphoreTake(_api_mutex, portMAX_DELAY);
    releaseAllLocked();
    xSemaphoreGive(_api_mutex);
}

bool PS2KeyboardDevice::dataReportingEnabled() { return _kb.data_reporting_enabled(); }
bool PS2KeyboardDevice::capsLockOn()           { return _kb.is_caps_lock_led_on(); }
bool PS2KeyboardDevice::numLockOn()            { return _kb.is_num_lock_led_on(); }
bool PS2KeyboardDevice::scrollLockOn()         { return _kb.is_scroll_lock_led_on(); }

#endif // PIN_KB_CLK
