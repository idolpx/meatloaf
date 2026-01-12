#ifndef C64LAYOUT_H
#define C64LAYOUT_H

#include <vector>
#include <cstdint>
#include "../model/PixelImage.h"
#include "../model/ColorMap.h"

/**
 * Concatenate multiple byte arrays.
 */
std::vector<uint8_t> concat(const std::vector<std::vector<uint8_t>>& arrayBuffers);

/**
 * Pad a buffer with a given number of bytes.
 */
std::vector<uint8_t> pad(const std::vector<uint8_t>& buffer, size_t numberOfBytes);

/**
 * Convert a PixelImage to a bitmap representation.
 */
std::vector<uint8_t> convertBitmap(const PixelImage& pixelImage);

/**
 * Convert screen RAM data from a PixelImage.
 */
std::vector<uint8_t> convertScreenram(
    const PixelImage& pixelImage,
    int lowerColorIndex,
    int upperColorIndex,
    int yOffset = 0
);

/**
 * Convert color RAM data from a PixelImage.
 */
std::vector<uint8_t> convertColorram(const PixelImage& pixelImage, int colorMapIndex);

#endif // C64LAYOUT_H
