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

// Numbered dial entries, so ATD5 reaches a stored host.
//
// Numbers are compared as strings because leading zeros are significant to a
// user who stored 007. Listing order is numeric, since that is what ATP prints.

#ifndef MEATLOAF_MODEM_PHONEBOOK
#define MEATLOAF_MODEM_PHONEBOOK

#ifdef ENABLE_MODEM

#include <cstdint>
#include <string>
#include <vector>

struct PhonebookEntry
{
    std::string number;  // the digits the user dials
    std::string host;
    uint16_t    port = 23;
    std::string mods;    // dial modifiers to apply, e.g. "T"
};

// Splits "host" or "host:port" into its parts. A bare host is port 23 -- every
// phase-1 target is a BBS and typing the port each time is friction that makes
// a feature go unused. Returns false for an empty host, a non-numeric port, or
// a port outside 1-65535.
bool phonebook_split_hostport(const std::string &in, std::string &host,
                              uint16_t &port);

class Phonebook
{
public:
    // Refuses a non-numeric or empty number, and a host/port that
    // phonebook_split_hostport() rejects. Replaces an existing number.
    bool store(const std::string &number, const std::string &hostport,
               const std::string &mods);

    // Null when the number is not stored.
    const PhonebookEntry *find(const std::string &number) const;

    bool erase(const std::string &number);

    // Sorted by number, shortest-then-lexicographic so 2 precedes 10.
    const std::vector<PhonebookEntry> &all() const { return entries_; }

    void clear() { entries_.clear(); }

private:
    void sort();

    std::vector<PhonebookEntry> entries_;
};

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_PHONEBOOK
