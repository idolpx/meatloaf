#include "IImageData.h"

IImageData::IImageData(int w, int h)
    : width(w), height(h), data(w * h * 4, 0) {} // Initialize with zeroes

uint8_t* IImageData::getPixel(int x, int y) {
    size_t index = (y * width + x) * 4;
    return &data[index]; // Returns a pointer to the first byte of the pixel (RGBA)
}

void IImageData::setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    size_t index = (y * width + x) * 4;
    data[index] = r;
    data[index + 1] = g;
    data[index + 2] = b;
    data[index + 3] = a;
}
