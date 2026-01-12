#ifndef AFLIPICTURE_H
#define AFLIPICTURE_H

#include <vector>
#include <memory>
#include <string>
#include "../model/PixelImage.h"
#include "IBinaryFormat.h"
#include "C64Layout.h"

class AFLIPicture : public IBinaryFormat {
public:
    AFLIPicture();

    std::string getFormatName() const override { return formatName; }
    std::string getDefaultExtension() const override { return defaultExtension; }
    
    void fromPixelImage(const PixelImage& pixelImage) override;
    std::vector<std::vector<uint8_t>> toMemoryMap() const override;

    std::string formatName = "afli";
    std::string defaultExtension = "afli";

private:
    std::vector<uint8_t> loadAddress;
    std::vector<std::vector<uint8_t>> screenRam;
    std::vector<uint8_t> bitmap;
};

#endif // AFLIPICTURE_H
