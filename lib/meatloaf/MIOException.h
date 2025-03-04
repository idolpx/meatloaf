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

#ifndef MEATLOAF_EXCEPTION
#define MEATLOAF_EXCEPTION

#include <exception>

// truned off by default: https://github.com/platformio/platform-ststm32/issues/402

// PIO_FRAMEWORK_ARDUINO_ENABLE_EXCEPTIONS - whre do I set this?
// https://github.com/esp8266/Arduino/blob/master/tools/platformio-build.py

struct IOException : public std::exception {
   const char * what () const throw () {
      return "IO";
   }
};

struct IllegalStateException : public IOException {
   const char * what () const throw () {
      return "Illegal State";
   }
};

struct FileNotFoundException : public IOException {
   const char * what () const throw () {
      return "Not found";
   }
};


#endif // MEATLOAF_EXCEPTION