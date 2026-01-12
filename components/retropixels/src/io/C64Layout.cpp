#include "C64Layout.h"

std::vector<uint8_t> concat(const std::vector<std::vector<uint8_t>>& arrayBuffers) {
    if (arrayBuffers.size() == 1) {
        return arrayBuffers[0];
    }

    std::vector<uint8_t> result;
    size_t totalSize = 0;

    // Compute total size
    for (const auto& buffer : arrayBuffers) {
        totalSize += buffer.size();
    }

    result.reserve(totalSize); // Optimize memory allocation

    // Copy each buffer into result
    for (const auto& buffer : arrayBuffers) {
        result.insert(result.end(), buffer.begin(), buffer.end());
    }

    return result;
}

std::vector<uint8_t> pad(const std::vector<uint8_t>& buffer, size_t numberOfBytes) {
    std::vector<uint8_t> padding(numberOfBytes, 0);
    return concat({buffer, padding});
}

std::vector<uint8_t> convertBitmap(const PixelImage& pixelImage) {
    size_t bitmapSize = (pixelImage.mode.width * pixelImage.mode.height) / pixelImage.mode.pixelsPerByte();
    std::vector<uint8_t> bitmap(bitmapSize);
    size_t bitmapIndex = 0;

    pixelImage.mode.forEachCell(0, [&](int x, int y) {
        pixelImage.mode.forEachCellRow(y, [&](int rowY) {
            // Pack one character's row worth of pixels into one byte
            pixelImage.mode.forEachByte(x, [&](int byteX) {
                uint8_t packedByte = 0;
                if (byteX >= pixelImage.mode.fliBugSize) {
                    pixelImage.mode.forEachPixel(byteX, [&](int pixelX, int shiftTimes) {
                        packedByte |= (pixelImage.mapPixelIndex(pixelX, rowY) << shiftTimes);
                    });
                }
                bitmap[bitmapIndex++] = packedByte;
            });
        });
    });

    return bitmap;
}

std::vector<uint8_t> convertScreenram(
    const PixelImage& pixelImage,
    int lowerColorIndex,
    int upperColorIndex,
    int yOffset
) {
    return pixelImage.extractAttributeData(yOffset, [&](int x, int y) {
        uint8_t upperColor = pixelImage.colorMaps[upperColorIndex].getIndexOrDefault(x, y);
        uint8_t lowerColor = pixelImage.colorMaps[lowerColorIndex].getIndexOrDefault(x, y);
        // Pack two colors into one byte
        return static_cast<uint8_t>(((upperColor << 4) & 0xF0) | (lowerColor & 0x0F));
    });
}

std::vector<uint8_t> convertColorram(const PixelImage& pixelImage, int colorMapIndex) {
    return pixelImage.extractAttributeData(0, [&](int x, int y) {
        return static_cast<uint8_t>(pixelImage.colorMaps[colorMapIndex].getIndexOrDefault(x, y) & 0x0F);
    });
}
