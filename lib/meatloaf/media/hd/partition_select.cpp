#include "partition_select.h"

#include "dhd.h"
#include "hdd.h"
#include "string_utils.h"

#include <cstdlib>

namespace hdpart {

static const char* cmd_type_label(uint8_t type)
{
    switch (type)
    {
        case 1:  return "NAT";
        case 2:  return "1541";
        case 3:  return "1571";
        case 4:  return "1581";
        case 0xFF: return "SYS";   // entry 0: listed, never selectable
        default: return "?";
    }
}

static const char* cfs_type_label(uint8_t type)
{
    switch (type)
    {
        case 0:  return "----";    // unformatted
        case 1:  return "CFS";
        case 2:  return "GEOS";
        default: return "?";       // 3-11 reserved
    }
}

Target targetFor(const std::string& path)
{
    Target t;

    std::string c = DHDImageRegistry::containerOf(path);
    if (!c.empty())
    {
        t.kind = Kind::CMD;
        t.container = c;
        return t;
    }

    c = HDDImageRegistry::containerOf(path);
    if (!c.empty())
    {
        t.kind = Kind::CFS;
        t.container = c;
    }
    return t;
}

bool list(const Target& t, std::vector<View>& out, std::string& disk_label)
{
    out.clear();
    disk_label.clear();

    if (t.kind == Kind::CMD)
    {
        auto img = DHDImageRegistry::obtain(t.container);
        if (img == nullptr || !img->valid)
            return false;
        disk_label = img->disk_label;
        for (const auto& p : img->parts)
        {
            View v;
            v.number = p.number;
            v.type_label = cmd_type_label(p.type);
            v.name = mstr::toUTF8(p.name);
            v.selected = (p.number == img->selected);
            v.selectable = (p.number != 0);
            out.push_back(v);
        }
        return true;
    }

    if (t.kind == Kind::CFS)
    {
        auto img = HDDImageRegistry::obtain(t.container);
        if (img == nullptr || !img->valid)
            return false;
        disk_label = img->disk_label;
        for (const auto& p : img->parts)
        {
            View v;
            v.number = p.number;
            v.type_label = cfs_type_label(p.type);
            v.name = p.name;           // CFS names are ASCII
            v.selected = (p.number == img->selected);
            v.selectable = (p.type == 1);
            out.push_back(v);
        }
        return true;
    }

    return false;
}

int resolve(const Target& t, const std::string& arg)
{
    if (t.kind == Kind::None || arg.empty())
        return -1;

    const long hi = (t.kind == Kind::CMD) ? 254 : 16;

    // Numbers first. Range-checked BEFORE any narrowing, per the project rule
    // against atoi/std::stoi on C64- or network-sourced input.
    bool numeric = arg.find_first_not_of("0123456789") == std::string::npos;
    if (numeric)
    {
        char* end = nullptr;
        long v = strtol(arg.c_str(), &end, 10);
        if (end != arg.c_str() && *end == '\0' && v >= 0 && v <= hi)
        {
            std::vector<View> parts;
            std::string label;
            if (list(t, parts, label))
            {
                for (const auto& p : parts)
                {
                    if (p.number == (uint8_t)v)
                        return (int)v;
                }
            }
        }
    }

    // Then names - a partition can legitimately be named "1571". Wildcards
    // are honoured, case-sensitively, by each registry's byName().
    if (t.kind == Kind::CMD)
    {
        auto img = DHDImageRegistry::obtain(t.container);
        if (img != nullptr)
        {
            const DHDPartition* p = img->byName(arg);
            if (p != nullptr)
                return (int)p->number;
        }
    }
    else
    {
        auto img = HDDImageRegistry::obtain(t.container);
        if (img != nullptr)
        {
            const HDDPartition* p = img->byName(arg);
            if (p != nullptr)
                return (int)p->number;
        }
    }

    return -1;
}

bool select(const Target& t, int number)
{
    // The valid ranges genuinely differ and must stay separate: a CMD HD holds
    // 1..254 with table entry 0 the system partition,
    // and CFS numbers its partitions 1..16 over the valid entries of a
    // 16-entry table. Both reserve 0 for "the currently selected partition",
    // so neither accepts it here.
    if (t.kind == Kind::CMD)
    {
        if (number < 1 || number > 254)
            return false;
        return DHDImageRegistry::select(t.container, (uint8_t)number);
    }

    if (t.kind == Kind::CFS)
    {
        if (number < 1 || number > 16)
            return false;
        return HDDImageRegistry::select(t.container, (uint8_t)number);
    }

    return false;
}

} // namespace hdpart
