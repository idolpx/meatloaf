#ifndef SPRITEPAD_H
#define SPRITEPAD_H

#include <vector>
#include <memory>
#include <string>
#include "IBinaryFormat.h"
#include "../model/PixelImage.h"

class SpritePad : public IBinaryFormat {
public:
    SpritePad();

    std::string getFormatName() const override { return "spritepad"; }
    std::string getDefaultExtension() const override { return "spd"; }

    void fromPixelImage(const PixelImage& pixelImage) override;
    std::vector<std::vector<uint8_t>> toMemoryMap() const override;

private:
    uint8_t backgroundColor;
    uint8_t multiColor1;
    uint8_t multiColor2;

    std::vector<std::unique_ptr<uint8_t[]>> sprites;

    static const std::vector<std::string> supportedModes;
};

#endif // SPRITEPAD_H
