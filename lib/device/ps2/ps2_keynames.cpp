#include "ps2_keynames.h"

#include <cctype>

namespace
{
    using ps2keys::Key;
    namespace sc = ps2dev::scancodes;

    struct Entry { const char *name; Key key; bool canonical; };

    // Canonical entries are listed by `ps2 keys`; aliases resolve but are not
    // listed.  Linear search -- ~90 entries is nothing, and this keeps the
    // whole table in flash with no heap and no static constructor.
    const Entry TABLE[] = {
        {"a",sc::K_A,true},{"b",sc::K_B,true},{"c",sc::K_C,true},{"d",sc::K_D,true},
        {"e",sc::K_E,true},{"f",sc::K_F,true},{"g",sc::K_G,true},{"h",sc::K_H,true},
        {"i",sc::K_I,true},{"j",sc::K_J,true},{"k",sc::K_K,true},{"l",sc::K_L,true},
        {"m",sc::K_M,true},{"n",sc::K_N,true},{"o",sc::K_O,true},{"p",sc::K_P,true},
        {"q",sc::K_Q,true},{"r",sc::K_R,true},{"s",sc::K_S,true},{"t",sc::K_T,true},
        {"u",sc::K_U,true},{"v",sc::K_V,true},{"w",sc::K_W,true},{"x",sc::K_X,true},
        {"y",sc::K_Y,true},{"z",sc::K_Z,true},
        {"0",sc::K_0,true},{"1",sc::K_1,true},{"2",sc::K_2,true},{"3",sc::K_3,true},
        {"4",sc::K_4,true},{"5",sc::K_5,true},{"6",sc::K_6,true},{"7",sc::K_7,true},
        {"8",sc::K_8,true},{"9",sc::K_9,true},

        {"enter",sc::K_RETURN,true},      {"return",sc::K_RETURN,false},
        {"escape",sc::K_ESCAPE,true},     {"esc",sc::K_ESCAPE,false},
        {"tab",sc::K_TAB,true},
        {"backspace",sc::K_BACKSPACE,true},
        {"space",sc::K_SPACE,true},
        {"insert",sc::K_INSERT,true},     {"ins",sc::K_INSERT,false},
        {"delete",sc::K_DELETE,true},     {"del",sc::K_DELETE,false},
        {"home",sc::K_HOME,true},
        {"end",sc::K_END,true},
        {"pageup",sc::K_PAGEUP,true},     {"pgup",sc::K_PAGEUP,false},
        {"pagedown",sc::K_PAGEDOWN,true}, {"pgdn",sc::K_PAGEDOWN,false},
        {"up",sc::K_UP,true},{"down",sc::K_DOWN,true},
        {"left",sc::K_LEFT,true},{"right",sc::K_RIGHT,true},

        {"f1",sc::K_F1,true},{"f2",sc::K_F2,true},{"f3",sc::K_F3,true},
        {"f4",sc::K_F4,true},{"f5",sc::K_F5,true},{"f6",sc::K_F6,true},
        {"f7",sc::K_F7,true},{"f8",sc::K_F8,true},{"f9",sc::K_F9,true},
        {"f10",sc::K_F10,true},{"f11",sc::K_F11,true},{"f12",sc::K_F12,true},

        // A bare modifier means the LEFT-hand key, which is what a host that
        // does not distinguish sides expects.
        {"ctrl",sc::K_LCTRL,true},   {"lctrl",sc::K_LCTRL,false},
        {"rctrl",sc::K_RCTRL,true},
        {"shift",sc::K_LSHIFT,true}, {"lshift",sc::K_LSHIFT,false},
        {"rshift",sc::K_RSHIFT,true},
        {"alt",sc::K_LALT,true},     {"lalt",sc::K_LALT,false},
        {"ralt",sc::K_RALT,true},
        {"gui",sc::K_LSUPER,true},   {"win",sc::K_LSUPER,false},
        {"lgui",sc::K_LSUPER,false}, {"rgui",sc::K_RSUPER,true},
        {"menu",sc::K_MENU,true},

        {"capslock",sc::K_CAPSLOCK,true},
        {"numlock",sc::K_NUMLOCK,true},
        {"scrolllock",sc::K_SCROLLOCK,true},
        {"printscreen",sc::K_PRINT,true}, {"print",sc::K_PRINT,false},
        {"pause",sc::K_PAUSE,true},

        {"backquote",sc::K_BACKQUOTE,true},
        {"minus",sc::K_MINUS,true},{"equals",sc::K_EQUALS,true},
        {"backslash",sc::K_BACKSLASH,true},
        {"leftbracket",sc::K_LEFTBRACKET,true},
        {"rightbracket",sc::K_RIGHTBRACKET,true},
        {"semicolon",sc::K_SEMICOLON,true},{"quote",sc::K_QUOTE,true},
        {"comma",sc::K_COMMA,true},{"period",sc::K_PERIOD,true},
        {"slash",sc::K_SLASH,true},
    };
    const size_t TABLE_LEN = sizeof(TABLE) / sizeof(TABLE[0]);

    std::string lower(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++)
            out += (char)tolower((unsigned char)s[i]);
        return out;
    }

    bool findInTable(const std::string &lowered, Key &out)
    {
        for (size_t i = 0; i < TABLE_LEN; i++)
            if (lowered == TABLE[i].name) { out = TABLE[i].key; return true; }
        return false;
    }
}

namespace ps2keys
{
    bool lookupKey(const std::string &name, Key &out, const Overrides *ov)
    {
        if (name.empty())
            return false;

        std::string n = lower(name);

        // One level of indirection only -- an override names a real key, not
        // another override, so a cycle is impossible by construction.
        if (ov)
        {
            Overrides::const_iterator it = ov->find(n);
            if (it != ov->end())
                return findInTable(lower(it->second), out);
        }

        return findInTable(n, out);
    }

    bool parseCombo(const std::string &spec, std::vector<Key> &out, const Overrides *ov)
    {
        out.clear();
        if (spec.empty())
            return false;

        size_t start = 0;
        while (true)
        {
            size_t plus = spec.find('+', start);
            std::string part = (plus == std::string::npos)
                                 ? spec.substr(start)
                                 : spec.substr(start, plus - start);
            Key k;
            if (!lookupKey(part, k, ov)) { out.clear(); return false; }
            out.push_back(k);

            if (plus == std::string::npos)
                break;
            start = plus + 1;
        }
        return true;
    }

    const char *keyName(Key k)
    {
        for (size_t i = 0; i < TABLE_LEN; i++)
            if (TABLE[i].key == k && TABLE[i].canonical)
                return TABLE[i].name;
        return nullptr;
    }

    void allNames(std::vector<const char *> &out)
    {
        out.clear();
        for (size_t i = 0; i < TABLE_LEN; i++)
            if (TABLE[i].canonical)
                out.push_back(TABLE[i].name);
    }
}
