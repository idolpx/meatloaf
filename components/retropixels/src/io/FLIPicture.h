#ifndef FLIPICTURE_H
#define FLIPICTURE_H

#include <vector>
#include <memory>
#include <string>
#include "../model/PixelImage.h"
#include "../io/IBinaryFormat.h"

/**
 * FLIPicture class representing an FLI image format.
 */
class FLIPicture : public IBinaryFormat {
public:
    std::string formatName = "fli";
    std::string defaultExtension = "fli";

    std::string getFormatName() const override { return formatName; }
    std::string getDefaultExtension() const override { return defaultExtension; }

private:
    std::vector<uint8_t> loadAddress;
    std::vector<uint8_t> colorRam;
    std::vector<std::vector<uint8_t>> screenRam;
    std::vector<uint8_t> bitmap;
    std::vector<uint8_t> background;

public:
    FLIPicture();

    /**
     * Convert a PixelImage to an FLIPicture.
     * @param pixelImage The input image.
     */
    void fromPixelImage(const PixelImage& pixelImage) override;

    /**
     * Convert to a sequence of byte vectors.
     * @return A sequence of 8-bit byte vectors.
     */
    std::vector<std::vector<uint8_t>> toMemoryMap() const override;
};

#endif // FLIPICTURE_H
