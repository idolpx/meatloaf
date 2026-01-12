#include "FLIPicture.h"
#include "../conversion/Converter.h"
#include "../model/ColorMap.h"
#include "../io/C64Layout.h"

FLIPicture::FLIPicture() {
    loadAddress.resize(2, 0);
}

void FLIPicture::fromPixelImage(const PixelImage& pixelImage) {
    loadAddress = {0, 0x3C};

    colorRam = convertColorram(pixelImage, 1);
    bitmap = convertBitmap(pixelImage);

    screenRam.resize(8);
    for (int i = 0; i < 8; ++i) {
        screenRam[i] = convertScreenram(pixelImage, 2, 3, i);
    }

    background = {static_cast<uint8_t>(pixelImage.colorMaps[0].getNonEmpty(0, 0))};
}

std::vector<std::vector<uint8_t>> FLIPicture::toMemoryMap() const {
    return {
        loadAddress,
        pad(colorRam, 24),
        pad(screenRam[0], 24),
        pad(screenRam[1], 24),
        pad(screenRam[2], 24),
        pad(screenRam[3], 24),
        pad(screenRam[4], 24),
        pad(screenRam[5], 24),
        pad(screenRam[6], 24),
        pad(screenRam[7], 24),
        bitmap,
        background
    };
}
