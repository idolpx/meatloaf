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

#ifndef LCD_H
#define LCD_H

#include "../../include/pinmap.h"

#ifdef PIN_TFT_MOSI

#include "hagl.h"
#include "hagl_hal.h"
#include <freertos/FreeRTOS.h>
#include <string>

class DisplayLCD
{
private:
    hagl_backend_t *backend;
    static DisplayLCD *instance_;

public:
    DisplayLCD() : backend(nullptr) { init_lcd(); instance_ = this; };
    ~DisplayLCD() { if (backend) hagl_close(backend); };

    static DisplayLCD *instance() { return instance_; }

    esp_err_t init_lcd();
    TickType_t show_jpeg(const char *file);
    TickType_t show_png(const char *file);
    void show_image(std::string filename);
};

extern DisplayLCD LCD;

#else // PIN_TFT_MOSI not defined — no display pins in this board's pinmap

#include <string>

class DisplayLCD
{
public:
    DisplayLCD() = default;
    ~DisplayLCD() = default;
    static DisplayLCD *instance() { return nullptr; }
    void show_image(std::string) {}
};

extern DisplayLCD LCD;

#endif // PIN_TFT_MOSI

#endif // LCD_H