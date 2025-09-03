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

#include <soc/gpio_num.h>

#include <st7789.h>
#include "fontx.h"
#include "decoders/bmpfile.h"
#include "decoders/decode_jpeg.h"
#include "decoders/decode_png.h"
#include "decoders/pngle.h"

#include "../../include/global_defines.h"
#include "../../include/pinmap.h"
#include "../../include/debug.h"

DisplayLCD LCD;

#ifdef ST7789_DRIVER

esp_err_t DisplayLCD::init_lcd()
{
	Debug_printv("DisplayLCD::init_lcd()");
	// // set font file
	// FontxFile fx16G[2];
	// FontxFile fx24G[2];
	// FontxFile fx32G[2];
	// FontxFile fx32L[2];
	// InitFontx(fx16G, SYSTEM_DIR "/lcd/ILGH16XB.FNT", ""); // 8x16Dot Gothic
	// InitFontx(fx24G, SYSTEM_DIR "/lcd/ILGH24XB.FNT", ""); // 12x24Dot Gothic
	// InitFontx(fx32G, SYSTEM_DIR "/lcd/ILGH32XB.FNT", ""); // 16x32Dot Gothic
	// InitFontx(fx32L, SYSTEM_DIR "/lcd/LATIN32B.FNT", ""); // 16x32Dot Latin

	// FontxFile fx16M[2];
	// FontxFile fx24M[2];
	// FontxFile fx32M[2];
	// InitFontx(fx16M, SYSTEM_DIR "/lcd/ILMH16XB.FNT", ""); // 8x16Dot Mincyo
	// InitFontx(fx24M, SYSTEM_DIR "/lcd/ILMH24XB.FNT", ""); // 12x24Dot Mincyo
	// InitFontx(fx32M, SYSTEM_DIR "/lcd/ILMH32XB.FNT", ""); // 16x32Dot Mincyo

	// Change SPI Clock Frequency
	//spi_clock_speed(40000000); // 40MHz
	//spi_clock_speed(60000000); // 60MHz
	spi_clock_speed(TFT_SPI_FREQUENCY);

	spi_master_init(&dev, PIN_TFT_MOSI, PIN_TFT_SCLK, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST, PIN_TFT_BL);
	lcdInit(&dev, TFT_WIDTH, TFT_HEIGHT, TFT_OFFSETX, TFT_OFFSETY);

#if TFT_INVERSION_ON
	Debug_printv("Enable Display Inversion");
	//lcdInversionOn(&dev);
	lcdInversionOff(&dev);
#endif

	return ESP_OK;
}

TickType_t DisplayLCD::show_jpeg(char * file, int width, int height) {
	TickType_t startTick, endTick, diffTick;
	startTick = xTaskGetTickCount();

	lcdSetFontDirection(&dev, 0);
	lcdFillScreen(&dev, BLACK);


	pixel_jpeg **pixels;
	int imageWidth;
	int imageHeight;
	esp_err_t err = decode_jpeg(&pixels, file, width, height, &imageWidth, &imageHeight);
	Debug_printv("decode_image err=%d imageWidth=%d imageHeight=%d", err, imageWidth, imageHeight);
	if (err == ESP_OK) {

		uint16_t _width = width;
		uint16_t _cols = 0;
		if (width > imageWidth) {
			_width = imageWidth;
			_cols = (width - imageWidth) / 2;
		}
		Debug_printv("_width=%d _cols=%d", _width, _cols);

		uint16_t _height = height;
		uint16_t _rows = 0;
		if (height > imageHeight) {
			_height = imageHeight;
			_rows = (height - imageHeight) / 2;
		}
		Debug_printv("_height=%d _rows=%d", _height, _rows);

		// allocate memory
		uint16_t *colors = (uint16_t*)malloc(sizeof(uint16_t) * _width);
		if (colors == NULL) {
			Debug_printv("Error allocating memory for colors");
			return 0;
		}

#if 0
		for(int y = 0; y < _height; y++){
			for(int x = 0;x < _width; x++){
				pixel_jpeg pixel = pixels[y][x];
				uint16_t color = rgb565(pixel.red, pixel.green, pixel.blue);
				lcdDrawPixel(dev, x+_cols, y+_rows, color);
			}
			//vTaskDelay(1);
		}
#endif

		for(int y = 0; y < _height; y++){
			for(int x = 0;x < _width; x++){
				//pixel_jpeg pixel = pixels[y][x];
				//colors[x] = rgb565(pixel.red, pixel.green, pixel.blue);
				colors[x] = pixels[y][x];
			}
			lcdDrawMultiPixels(&dev, _cols, y+_rows, _width, colors);
			//vTaskDelay(1);
		}

		lcdDrawFinish(&dev);
		free(colors);
		release_image(&pixels, width, height);
		Debug_printv("Finish");
	} else {
		Debug_printv("decode_jpeg fail=%d", err);
	}

	endTick = xTaskGetTickCount();
	diffTick = endTick - startTick;
	Debug_printv("elapsed time[ms]:%" PRIu32, diffTick * portTICK_PERIOD_MS);
	return diffTick;
}


TickType_t DisplayLCD::show_png(char * file, int width, int height) {
	TickType_t startTick, endTick, diffTick;
	startTick = xTaskGetTickCount();

	lcdSetFontDirection(&dev, 0);
	lcdFillScreen(&dev, BLACK);

	// open PNG file
	FILE* fp = fopen(file, "rb");
	if (fp == NULL) {
		Debug_printv("File not found [%s]", file);
		return 0;
	}

	pngle_t *pngle = pngle_new(width, height);
	if (pngle == NULL) {
		Debug_printv("pngle_new fail");
		fclose(fp);
		return 0;
	}
	pngle_set_init_callback(pngle, png_init);
	pngle_set_draw_callback(pngle, png_draw);
	pngle_set_done_callback(pngle, png_finish);

	double display_gamma = 2.2;
	pngle_set_display_gamma(pngle, display_gamma);

	char buf[1024];
	size_t remain = 0;
	while (!feof(fp)) {
		if (remain >= sizeof(buf)) {
			Debug_printv("Buffer exceeded");
			//while(1) vTaskDelay(1);
		}

		int len = fread(buf + remain, 1, sizeof(buf) - remain, fp);
		if (len <= 0) {
			//printf("EOF\n");
			break;
		}

		int fed = pngle_feed(pngle, buf, remain + len);
		if (fed < 0) {
			Debug_printv("ERROR; %s", pngle_error(pngle));
			//while(1) vTaskDelay(1);
		}

		remain = remain + len - fed;
		if (remain > 0) memmove(buf, buf + fed, remain);
	}

	fclose(fp);

	uint16_t _width = width;
	uint16_t _cols = 0;
	if (width > pngle->imageWidth) {
		_width = pngle->imageWidth;
		_cols = (width - pngle->imageWidth) / 2;
	}
	Debug_printv("_width=%d _cols=%d", _width, _cols);

	uint16_t _height = height;
	uint16_t _rows = 0;
	if (height > pngle->imageHeight) {
			_height = pngle->imageHeight;
			_rows = (height - pngle->imageHeight) / 2;
	}
	Debug_printv("_height=%d _rows=%d", _height, _rows);

	// allocate memory
	uint16_t *colors = (uint16_t*)malloc(sizeof(uint16_t) * _width);
	if (colors == NULL) {
		Debug_printv("Error allocating memory for colors");
		return 0;
	}

#if 0
	for(int y = 0; y < _height; y++){
		for(int x = 0;x < _width; x++){
			pixel_png pixel = pngle->pixels[y][x];
			uint16_t color = rgb565(pixel.red, pixel.green, pixel.blue);
			lcdDrawPixel(dev, x+_cols, y+_rows, color);
		}
	}
#endif

	for(int y = 0; y < _height; y++){
		for(int x = 0;x < _width; x++){
			//pixel_png pixel = pngle->pixels[y][x];
			//colors[x] = rgb565(pixel.red, pixel.green, pixel.blue);
			colors[x] = pngle->pixels[y][x];
		}
		lcdDrawMultiPixels(&dev, _cols, y+_rows, _width, colors);
		//vTaskDelay(1);
	}
	lcdDrawFinish(&dev);
	free(colors);
	pngle_destroy(pngle, width, height);

	endTick = xTaskGetTickCount();
	diffTick = endTick - startTick;
	Debug_printv("elapsed time[ms]:%" PRIu32, diffTick * portTICK_PERIOD_MS);
	return diffTick;
}

#endif // ST7789_DRIVER

void DisplayLCD::show_image(std::string filename)
{
#ifdef ST7789_DRIVER
    LCD.show_png( (char *)filename.c_str() );
#else
    Debug_printv("DisplayLEDs::show_image() called but ST7789_DRIVER is not defined.");
#endif
}

#endif // ENABLE_DISPLAY