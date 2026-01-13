#ifndef QUANTIZER_H
#define QUANTIZER_H

#include <vector>
#include <functional>
#include <limits>
#include <cmath>
#include "../model/IImageData.h"
#include "../model/Palette.h"
#include "../model/Pixels.h"

std::vector<double> convertColorSpace(const std::vector<int>& color);

class Quantizer {
private:
    const std::function<std::vector<double>(const std::vector<int>&)> colorspace;
    const Palette& palette;
    mutable std::vector<std::vector<double>> paletteCache; // Cache converted palette colors
    mutable bool cacheInitialized;

    void initializePaletteCache() const;

public:
    Quantizer(const Palette& palette, const std::function<std::vector<double>(const std::vector<int>&)>& colorspace);

    double distanceSquared(const std::vector<int>& realPixel, int paletteIndex) const;
    int quantizePixel(const std::vector<int>& pixel) const;
    std::vector<int> quantizeImage(const IImageData& image) const;
};

#endif // QUANTIZER_H
