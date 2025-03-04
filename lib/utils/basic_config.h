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

#ifndef MEATLOAF_UTILS_BASIC_CONFIG
#define MEATLOAF_UTILS_BASIC_CONFIG

#include <cstring>
#include <unordered_map>

#include "meatloaf.h"
#include "meat_buffer.h"


class BasicConfigReader {
public:
    std::unordered_map<std::string, std::string>* entries;

    BasicConfigReader() {
        entries = new std::unordered_map<std::string, std::string>();
    }

    ~BasicConfigReader() {
        delete entries;
    }

    void read(std::string name);
    std::string get(std::string key);
};

#endif /* MEATLOAF_UTILS_BASIC_CONFIG */
