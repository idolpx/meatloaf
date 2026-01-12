#include "PixelImage.h"

PixelImage::PixelImage(const GraphicMode& mode) : mode(mode) {
    pixelIndex.resize(mode.height, std::vector<int>(mode.width, -1)); // Initialize with -1
}

int PixelImage::peek(int x, int y) const {
    if (x < 0 || x >= mode.width || y < 0 || y >= mode.height) {
        return -1;
    }
    int colorMapIndex = pixelIndex[y][x];
    if (colorMapIndex == -1) {
        return -1;
    }
    return colorMaps[colorMapIndex].get(x, y).value_or(-1);
}

void PixelImage::addColorMap(int resXVal, int resYVal) {
    if (resXVal == -1) resXVal = mode.width;
    if (resYVal == -1) resYVal = mode.height;
    colorMaps.emplace_back(mode.width, mode.height, resXVal, resYVal);
}

int PixelImage::mapPixelIndex(int x, int y) const {
    if (x >= mode.width || x < 0) {
        throw std::runtime_error("x value out of bounds: " + std::to_string(x));
    }
    if (y >= mode.height || y < 0) {
        throw std::runtime_error("y value out of bounds: " + std::to_string(y));
    }
    return mode.indexMap.at(pixelIndex[y][x]);
}

std::vector<uint8_t> PixelImage::extractAttributeData(int yOffset, const std::function<int(int, int)>& callback) const {
    std::vector<uint8_t> result(1000, 0);
    int index = 0;

    mode.forEachCell(yOffset, [&](int x, int y) {
        result[index] = (x >= mode.fliBugSize) ? callback(x, y) : 0;
        index++;
    });

    return result;
}

bool PixelImage::isHires() const {
    return mode.pixelWidth == 1;
}

std::vector<PixelImage> PixelImage::debugColorMaps() const {
    std::vector<PixelImage> result;

    for (const auto& colorMap : colorMaps) {
        PixelImage pixelImage(mode);
        pixelImage.colorMaps.push_back(colorMap);
        pixelImage.pixelIndex.assign(mode.height, std::vector<int>(mode.width, 0));
        result.push_back(pixelImage);
    }

    return result;
}
