#include "ArtStudioPicture.h"

ArtStudioPicture::ArtStudioPicture() {
    loadAddress.resize(2, 0);
}

void ArtStudioPicture::fromPixelImage(const PixelImage& pixelImage) {
    loadAddress = {0, 0x20};

    bitmap = convertBitmap(pixelImage);
    screenRam = convertScreenram(pixelImage, 0, 1);

    magic = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

std::vector<std::vector<uint8_t>> ArtStudioPicture::toMemoryMap() const {
    return {loadAddress, bitmap, screenRam, magic};
}
