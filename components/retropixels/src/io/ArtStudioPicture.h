#ifndef ARTSTUDIOPICTURE_H
#define ARTSTUDIOPICTURE_H

#include <vector>
#include <memory>
#include <string>
#include "../model/PixelImage.h"
#include "IBinaryFormat.h"
#include "C64Layout.h"

class ArtStudioPicture : public IBinaryFormat {
public:
    ArtStudioPicture();

    std::string getFormatName() const override { return formatName; }
    std::string getDefaultExtension() const override { return defaultExtension; }
    
    void fromPixelImage(const PixelImage& pixelImage) override;
    std::vector<std::vector<uint8_t>> toMemoryMap() const override;

    std::string formatName = "artstudio";
    std::string defaultExtension = "art";

private:
    std::vector<uint8_t> loadAddress;
    std::vector<uint8_t> bitmap;
    std::vector<uint8_t> screenRam;
    std::vector<uint8_t> magic;
};

#endif // ARTSTUDIOPICTURE_H
