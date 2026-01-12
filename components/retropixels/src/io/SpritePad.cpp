#include "SpritePad.h"
#include "C64Layout.h"
#include "../conversion/Converter.h"

const std::vector<std::string> SpritePad::supportedModes = {
    "c64HiresSprites",
    "c64MulticolorSprites",
    "c64ThreecolorSprites",
    "c64TwocolorSprites"
};

SpritePad::SpritePad() : backgroundColor(0), multiColor1(0), multiColor2(0) {}

void SpritePad::fromPixelImage(const PixelImage& pixelImage) {
    const std::vector<uint8_t>& bitmap = convertBitmap(pixelImage);

    const bool isMulticolor = pixelImage.mode.pixelWidth == 2;

    backgroundColor = pixelImage.colorMaps[0].getNonEmpty(0, 0);
    multiColor1 = isMulticolor ? pixelImage.colorMaps[1].getNonEmpty(0, 0) : 0;
    multiColor2 = isMulticolor ? pixelImage.colorMaps[2].getNonEmpty(0, 0) : 0;

    const size_t spriteColorMapIndex = isMulticolor ? 3 : 1;

    size_t byteCounter = 0;
    pixelImage.mode.forEachCell(0, [&](size_t x, size_t y) {
        sprites.push_back(std::make_unique<uint8_t[]>(64));
        std::copy(bitmap.begin() + byteCounter, bitmap.begin() + byteCounter + 63, sprites.back().get());
        byteCounter += 63;

        const uint8_t modeFlag = isMulticolor ? 0x80 : 0;
        const uint8_t spriteColor = pixelImage.colorMaps[spriteColorMapIndex].getNonEmpty(x, y) & 0x0f;
        sprites.back()[63] = modeFlag | spriteColor;
    });
}

std::vector<std::vector<uint8_t>> SpritePad::toMemoryMap() const {
    std::vector<std::vector<uint8_t>> result = {
        {backgroundColor, multiColor1, multiColor2}
    };
    for (const auto& sprite : sprites) {
        result.push_back(std::vector<uint8_t>(sprite.get(), sprite.get() + 64));
    }
    return result;
}
