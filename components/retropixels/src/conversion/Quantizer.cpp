#include "Quantizer.h"

Quantizer::Quantizer(const Palette& palette, const std::function<std::vector<double>(const std::vector<int>&)>& colorspace)
    : colorspace(colorspace), palette(palette), cacheInitialized(false) {}

void Quantizer::initializePaletteCache() const {
    if (cacheInitialized) return;
    
    // Pre-convert all palette colors to the target colorspace
    paletteCache.resize(palette.colors.size());
    for (size_t i = 0; i < palette.colors.size(); ++i) {
        paletteCache[i] = colorspace(palette.colors[i]);
    }
    cacheInitialized = true;
}

double Quantizer::distanceSquared(const std::vector<int>& realPixel, int paletteIndex) const {
    if (!cacheInitialized) {
        initializePaletteCache();
    }
    
    // Convert input pixel once
    const std::vector<double> realPixelConverted = colorspace(realPixel);
    
    // Use cached converted palette color
    const std::vector<double>& palettePixelConverted = paletteCache[paletteIndex];

    // Calculate squared distance (avoid sqrt for speed)
    double sum = 0.0;
    for (size_t i = 0; i < realPixelConverted.size() && i < palettePixelConverted.size(); ++i) {
        const double diff = realPixelConverted[i] - palettePixelConverted[i];
        sum += diff * diff;
    }

    return sum;
}

int Quantizer::quantizePixel(const std::vector<int>& pixel) const {
    if (!cacheInitialized) {
        initializePaletteCache();
    }
    
    int bestIndex = 0;
    double minDistSq = std::numeric_limits<double>::max();

    // Loop over enabled palette indices
    for (size_t i = 0; i < palette.enabled.size(); ++i) {
        const int index = palette.enabled[i];
        const double distSq = distanceSquared(pixel, index);
        if (distSq < minDistSq) {
            minDistSq = distSq;
            bestIndex = index;
        }
    }

    return bestIndex;
}

std::vector<int> Quantizer::quantizeImage(const IImageData& image) const {
    if (!cacheInitialized) {
        initializePaletteCache();
    }
    
    std::vector<int> result(image.width * image.height);
    const int totalPixels = image.width * image.height;

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const int index = quantizePixel(peek(image, x, y));
            result[y * image.width + x] = index;
        }
    }

    return result;
}
