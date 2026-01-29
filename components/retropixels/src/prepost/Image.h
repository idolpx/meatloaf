#ifndef IMAGE_H
#define IMAGE_H

#include <vector>
#include <cstdint>
#include "../model/IImageData.h"

struct Image : public IImageData {
    Image(int w, int h);
    void setPixel(int x, int y, const std::vector<uint8_t>& color);
    std::vector<uint8_t> getPixel(int x, int y) const;
};

#endif // IMAGE_H
