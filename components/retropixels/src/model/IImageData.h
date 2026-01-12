#ifndef IIMAGEDATA_H
#define IIMAGEDATA_H

#include <vector>
#include <cstdint>

class IImageData {
public:
    int width;
    int height;
    std::vector<uint8_t> data; // Store pixel data as a flat array

    IImageData(int w, int h);

    uint8_t* getPixel(int x, int y);
    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
};

#endif // IIMAGEDATA_H
