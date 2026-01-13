#ifndef KOALA_PICTURE_H
#define KOALA_PICTURE_H

#include <vector>
#include <cstdint>
#include <string>
#include "../model/PixelImage.h"
#include "../io/IBinaryFormat.h"

class KoalaPicture : public IBinaryFormat {
public:
    KoalaPicture();

    std::string getFormatName() const override { return "koala"; }
    std::string getDefaultExtension() const override { return "kla"; }

    void fromPixelImage(const PixelImage& pixelImage) override;
    void read(const std::vector<uint8_t>& arrayBuffer);
    std::vector<std::vector<uint8_t>> toMemoryMap() const override;

private:
    std::vector<uint8_t> loadAddress;
    std::vector<uint8_t> bitmap;
    std::vector<uint8_t> screenRam;
    std::vector<uint8_t> colorRam;
    std::vector<uint8_t> background;
};

#endif // KOALA_PICTURE_H
