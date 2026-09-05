// PS/2 key-name lookup.
//
// Deliberately depends on nothing but <string>, <vector>, <map> and
// scan_codes_set_2.h (which includes only <stdint.h>), so it compiles in the
// native test environment where lib/device is otherwise absent.
#ifndef PS2_KEYNAMES_H
#define PS2_KEYNAMES_H

#include <map>
#include <string>
#include <vector>

#include "scan_codes_set_2.h"

namespace ps2keys
{
    using Key = ps2dev::scancodes::Key;

    // Maps a key NAME onto another key NAME.  The DTV's scancode-to-C64-key
    // table is a bench finding, so C64 names (runstop, restore, commodore...)
    // are bound at runtime from devices.ps2.keymap rather than guessed here.
    using Overrides = std::map<std::string, std::string>;

    // Case-insensitive.  Returns false for an unrecognised name, for an empty
    // name, and for an override that points at a name we do not know.
    bool lookupKey(const std::string &name, Key &out, const Overrides *ov = nullptr);

    // "ctrl+alt+del" -> {K_LCTRL, K_LALT, K_DELETE}, in the order written.
    // False if any member is unknown or empty.
    bool parseCombo(const std::string &spec, std::vector<Key> &out,
                    const Overrides *ov = nullptr);

    // Canonical name for a key, or nullptr if it has none.
    const char *keyName(Key k);

    // Every canonical name, for `ps2 keys`.  Aliases are not listed.
    void allNames(std::vector<const char *> &out);
}

#endif // PS2_KEYNAMES_H
