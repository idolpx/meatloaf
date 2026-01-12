#include "KoalaPicture.h"
#include "../io/C64Layout.h"
#include "../conversion/Converter.h"
#include <stdexcept>
#include <algorithm>

KoalaPicture::KoalaPicture() {
    loadAddress.resize(2, 0);
}

void KoalaPicture::fromPixelImage(const PixelImage& pixelImage) {
    // Koala format expects 160x200 logical pixels (320x200 physical for multicolor)
    if (pixelImage.mode.width != 160 || pixelImage.mode.height != 200) {
        // Return early if dimensions are wrong (exceptions disabled in ESP-IDF)
        return;
    }

    loadAddress = {0, 0x60};
    bitmap = convertBitmap(pixelImage);
    screenRam = convertScreenram(pixelImage, 2, 1);
    colorRam = convertColorram(pixelImage, 3);

    // Ensure background color is valid
    uint8_t bgColor = pixelImage.colorMaps.empty() ? 0 : pixelImage.colorMaps[0].getNonEmpty(0, 0);
    background = {bgColor};
}

void KoalaPicture::read(const std::vector<uint8_t>& arrayBuffer) {
    constexpr size_t expectedSize = 10003;
    if (arrayBuffer.size() < expectedSize) {
        // Return early if buffer too small (exceptions disabled in ESP-IDF)
        return;
    }

    auto it = arrayBuffer.begin();
    std::copy(it, it + 2, std::back_inserter(loadAddress));
    std::copy(it + 2, it + 8002, std::back_inserter(bitmap));
    std::copy(it + 8002, it + 9002, std::back_inserter(screenRam));
    std::copy(it + 9002, it + 10002, std::back_inserter(colorRam));
    background = {arrayBuffer[10002]};
}

std::vector<std::vector<uint8_t>> KoalaPicture::toMemoryMap() const {
    return {loadAddress, bitmap, screenRam, colorRam, background};
}
