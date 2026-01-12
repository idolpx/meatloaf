#include "C64Writer.h"

#include "C64Layout.h"
#include "AFLIPicture.h"
#include "FLIPicture.h"
#include "ArtStudioPicture.h"
#include "KoalaPicture.h"
#include "SpritePad.h"
#include "../model/PixelImage.h"
#include "IBinaryFormat.h"


std::unique_ptr<IBinaryFormat> toBinary(const PixelImage& pixelImage) {
    std::unique_ptr<IBinaryFormat> result;

    if (pixelImage.mode.id == "bitmap") {
        if (pixelImage.isHires()) {
            result = std::make_unique<ArtStudioPicture>();
        } else {
            result = std::make_unique<KoalaPicture>();
        }
    }

    if (pixelImage.mode.id == "fli") {
        if (pixelImage.isHires()) {
            result = std::make_unique<AFLIPicture>();
        } else {
            result = std::make_unique<FLIPicture>();
        }
    }

    if (pixelImage.mode.id == "sprites") {
        result = std::make_unique<SpritePad>();
    }

    if (result) {
        result->fromPixelImage(pixelImage);
        return result;
    }

    throw std::runtime_error("Output format is not supported for mode " + pixelImage.mode.id);
}

std::vector<uint8_t> toBuffer(const IBinaryFormat& image) {
    return concat(image.toMemoryMap());
}
