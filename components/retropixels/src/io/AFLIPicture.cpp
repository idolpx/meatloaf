#include "AFLIPicture.h"

AFLIPicture::AFLIPicture() {
    loadAddress.resize(2, 0);
}

void AFLIPicture::fromPixelImage(const PixelImage& pixelImage) {
    loadAddress = {0, 0x40};

    bitmap = convertBitmap(pixelImage);
    screenRam.resize(8);

    for (int i = 0; i < 8; i++) {
        screenRam[i] = convertScreenram(pixelImage, 0, 1, i);
    }
}

std::vector<std::vector<uint8_t>> AFLIPicture::toMemoryMap() const {
    return {
        loadAddress,
        pad(screenRam[0], 24),
        pad(screenRam[1], 24),
        pad(screenRam[2], 24),
        pad(screenRam[3], 24),
        pad(screenRam[4], 24),
        pad(screenRam[5], 24),
        pad(screenRam[6], 24),
        pad(screenRam[7], 24),
        bitmap
    };
}
