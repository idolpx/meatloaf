#include "Quantizer.h"

Quantizer::Quantizer(const Palette& palette, const std::function<std::vector<double>(const std::vector<int>&)>& colorspace)
    : colorspace(colorspace), palette(palette) {}

double Quantizer::distance(const std::vector<int>& realPixel, int paletteIndex) const {
    const std::vector<double> realPixelConverted = colorspace(realPixel);
    const std::vector<int>& palettePixelInt = palette.colors[paletteIndex];
    
    // Convert palette pixel int to double
    std::vector<double> palettePixel(palettePixelInt.size());
    for (size_t i = 0; i < palettePixelInt.size(); ++i) {
        palettePixel[i] = static_cast<double>(palettePixelInt[i]);
    }
    
    // Apply colorspace conversion to palette pixel
    std::vector<double> palettePixelConverted = colorspace(palettePixelInt);

    double sum = 0.0;
    for (size_t i = 0; i < realPixelConverted.size() && i < palettePixelConverted.size(); ++i) {
        const double diff = realPixelConverted[i] - palettePixelConverted[i];
        sum += diff * diff;
    }

    return std::sqrt(sum);
}

int Quantizer::quantizePixel(const std::vector<int>& pixel) const {
    int bestIndex = 0;
    double minDistance = std::numeric_limits<double>::max();

    for (size_t i = 0; i < palette.enabled.size(); ++i) {
        const int index = palette.enabled[i];
        const double dist = distance(pixel, index);
        if (dist < minDistance) {
            minDistance = dist;
            bestIndex = index;
        }
    }

    return bestIndex;
}

std::vector<int> Quantizer::quantizeImage(const IImageData& image) const {
    std::vector<int> result(image.width * image.height);

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const int index = quantizePixel(peek(image, x, y));
            result[y * image.width + x] = index;
        }
    }

    return result;
}
