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

#include "phonebook.h"

#ifdef ENABLE_MODEM

#include <algorithm>
#include <cstdlib>

namespace
{

bool all_digits(const std::string &s)
{
    if (s.empty())
        return false;
    for (char c : s)
    {
        if (c < '0' || c > '9')
            return false;
    }
    return true;
}

} // namespace

bool phonebook_split_hostport(const std::string &in, std::string &host,
                              uint16_t &port)
{
    if (in.empty())
        return false;

    size_t colon = in.rfind(':');
    if (colon == std::string::npos)
    {
        host = in;
        port = 23;
        return true;
    }

    std::string h = in.substr(0, colon);
    std::string p = in.substr(colon + 1);
    if (h.empty() || !all_digits(p))
        return false;

    // strtol, not std::stoi: ESP-IDF builds -fno-exceptions, so a throwing
    // conversion on malformed input becomes std::terminate.
    const char *start = p.c_str();
    char *end = nullptr;
    long v = std::strtol(start, &end, 10);
    if (end == start || *end != '\0' || v < 1 || v > 65535)
        return false;

    host = h;
    port = (uint16_t)v;
    return true;
}

void Phonebook::sort()
{
    // Shortest first, then lexicographic within a length -- that orders 1, 2,
    // 10 the way a user reads them, which plain lexicographic would not.
    std::sort(entries_.begin(), entries_.end(),
              [](const PhonebookEntry &a, const PhonebookEntry &b) {
                  if (a.number.size() != b.number.size())
                      return a.number.size() < b.number.size();
                  return a.number < b.number;
              });
}

bool Phonebook::store(const std::string &number, const std::string &hostport,
                      const std::string &mods)
{
    if (!all_digits(number))
        return false;

    std::string host;
    uint16_t port = 23;
    if (!phonebook_split_hostport(hostport, host, port))
        return false;

    for (auto &e : entries_)
    {
        if (e.number == number)
        {
            e.host = host;
            e.port = port;
            e.mods = mods;
            return true;
        }
    }

    PhonebookEntry e;
    e.number = number;
    e.host = host;
    e.port = port;
    e.mods = mods;
    entries_.push_back(e);
    sort();
    return true;
}

const PhonebookEntry *Phonebook::find(const std::string &number) const
{
    for (const auto &e : entries_)
    {
        if (e.number == number)
            return &e;
    }
    return nullptr;
}

bool Phonebook::erase(const std::string &number)
{
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        if (entries_[i].number == number)
        {
            entries_.erase(entries_.begin() + i);
            return true;
        }
    }
    return false;
}

#endif // ENABLE_MODEM
