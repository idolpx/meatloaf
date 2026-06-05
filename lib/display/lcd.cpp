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

#ifdef ENABLE_DISPLAY

#include "lcd.h"

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "hagl.h"
#include "hagl_hal.h"

#include "decoders/pngle.h"
#include "decoders/decode_png.h"

#include "../../include/global_defines.h"
#include "../../include/debug.h"

DisplayLCD LCD;

esp_err_t DisplayLCD::init_lcd()
{
    Debug_printv("DisplayLCD::init_lcd()");
    backend = hagl_init();
    if (backend == nullptr) {
        Debug_printv("hagl_init() failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

TickType_t DisplayLCD::show_jpeg(const char *file)
{
    TickType_t startTick = xTaskGetTickCount();

    hagl_clear(backend);
    uint32_t result = hagl_load_image(backend, 0, 0, file);
    if (result != HAGL_OK) {
        Debug_printv("hagl_load_image failed: %lu", result);
    }
    hagl_flush(backend);

    TickType_t diffTick = xTaskGetTickCount() - startTick;
    Debug_printv("elapsed time[ms]:%" PRIu32, diffTick * portTICK_PERIOD_MS);
    return diffTick;
}

TickType_t DisplayLCD::show_png(const char *file)
{
    TickType_t startTick = xTaskGetTickCount();

    hagl_clear(backend);

    FILE *fp = fopen(file, "rb");
    if (fp == NULL) {
        Debug_printv("File not found [%s]", file);
        return 0;
    }

    int width  = DISPLAY_WIDTH;
    int height = DISPLAY_HEIGHT;

    pngle_t *pngle = pngle_new(width, height);
    if (pngle == NULL) {
        Debug_printv("pngle_new fail");
        fclose(fp);
        return 0;
    }
    pngle_set_init_callback(pngle, png_init);
    pngle_set_draw_callback(pngle, png_draw);
    pngle_set_done_callback(pngle, png_finish);
    pngle_set_display_gamma(pngle, 2.2);

    char buf[1024];
    size_t remain = 0;
    while (!feof(fp)) {
        if (remain >= sizeof(buf)) {
            Debug_printv("Buffer exceeded");
        }
        int len = fread(buf + remain, 1, sizeof(buf) - remain, fp);
        if (len <= 0) break;

        int fed = pngle_feed(pngle, buf, remain + len);
        if (fed < 0) {
            Debug_printv("ERROR: %s", pngle_error(pngle));
        }
        remain = remain + len - fed;
        if (remain > 0) memmove(buf, buf + fed, remain);
    }
    fclose(fp);

    uint16_t _width = (uint16_t)width;
    uint16_t _cols  = 0;
    if (width > (int)pngle->imageWidth) {
        _width = pngle->imageWidth;
        _cols  = (uint16_t)((width - pngle->imageWidth) / 2);
    }

    uint16_t _height = (uint16_t)height;
    uint16_t _rows   = 0;
    if (height > (int)pngle->imageHeight) {
        _height = pngle->imageHeight;
        _rows   = (uint16_t)((height - pngle->imageHeight) / 2);
    }

    for (int y = 0; y < _height; y++) {
        for (int x = 0; x < _width; x++) {
            hagl_put_pixel(backend, x + _cols, y + _rows, (hagl_color_t)pngle->pixels[y][x]);
        }
    }
    hagl_flush(backend);

    pngle_destroy(pngle, width, height);

    TickType_t diffTick = xTaskGetTickCount() - startTick;
    Debug_printv("elapsed time[ms]:%" PRIu32, diffTick * portTICK_PERIOD_MS);
    return diffTick;
}

void DisplayLCD::show_image(std::string filename)
{
    Debug_printv("[%s]", filename.c_str());
    std::string ext = filename.substr(filename.find_last_of('.') + 1);
    if (ext == "jpg" || ext == "jpeg") {
        show_jpeg(filename.c_str());
    } else {
        show_png(filename.c_str());
    }
}

#endif // ENABLE_DISPLAY
