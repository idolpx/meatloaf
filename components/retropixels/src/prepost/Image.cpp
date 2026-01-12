#include "Image.h"

Image::Image(int w, int h) : IImageData(w, h) {
    pixels.resize(h, std::vector<std::vector<uint8_t>>(w, std::vector<uint8_t>(4, 255)));
}

void Image::setPixel(int x, int y, const std::vector<uint8_t>& color) {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        pixels[y][x] = color;
        // Also update the flat data array for IImageData compatibility
        int index = (y * width + x) * 4;
        if (index + 3 < static_cast<int>(data.size())) {
            data[index] = color[0];
            data[index + 1] = color[1];
            data[index + 2] = color[2];
            data[index + 3] = color[3];
        }
    }
}

std::vector<uint8_t> Image::getPixel(int x, int y) const {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        return pixels[y][x];
    }
    return {0, 0, 0, 255}; // Default to black
}
