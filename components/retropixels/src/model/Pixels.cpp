#include "Pixels.h"

const std::vector<int> emptyPixel = {0, 0, 0, 0};

std::vector<int> add(const std::vector<int>& one, const std::vector<int>& other) {
    std::vector<int> result(4);
    for (size_t i = 0; i < 4; ++i) {
        result[i] = std::clamp(one[i] + other[i], 0, 255);
    }
    return result;
}

void poke(IImageData& imageData, int x, int y, const std::vector<int>& pixel) {
    if (pixel.empty()) {
        return;
    }

    const int index = coordsToIndex(imageData, x, y);
    if (index >= 0 && index < static_cast<int>(imageData.data.size())) {
        imageData.data[index] = pixel[0];
        imageData.data[index + 1] = pixel[1];
        imageData.data[index + 2] = pixel[2];
        imageData.data[index + 3] = pixel[3];
    }
}

std::vector<int> peekAtIndex(const IImageData& imageData, int index) {
    if (index >= 0 && index < static_cast<int>(imageData.data.size())) {
        return {
            imageData.data[index],
            imageData.data[index + 1],
            imageData.data[index + 2],
            imageData.data[index + 3]
        };
    }
    return emptyPixel;
}

std::vector<int> peek(const IImageData& imageData, int x, int y) {
    return peekAtIndex(imageData, coordsToIndex(imageData, x, y));
}

int coordsToIndex(const IImageData& imageData, int x, int y) {
    int index = y * (imageData.width * 4) + x * 4;
    if (index < 0 || index >= static_cast<int>(imageData.data.size())) {
        // Return 0 as safe default (exceptions disabled in ESP-IDF)
        return 0;
    }
    return index;
}

int cap(int pixelChannel) {
    return std::clamp(pixelChannel, 0, 255);
}
