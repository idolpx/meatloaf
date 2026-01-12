#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

#include <string>
#include "Image.h"
#include "../model/PixelImage.h"
#include "../model/GraphicMode.h"
#include "../model/Palette.h"

namespace retropixels
{
    enum class ScaleMode {
        NONE,
        FILL
    };

    Image readImage(const std::string& filename, GraphicMode& graphicMode, ScaleMode scaleMode);
    bool writeImage(const PixelImage& pixelImage, const std::string& filename, const Palette& palette, const std::string& format = "png", int quality = 90);
    Image toImage(const PixelImage& pixelImage, const Palette& palette);
    Image resizeImage(const Image& inputImage, int newWidth, int newHeight);
    Image resizeImageNearestNeighbor(const Image& inputImage, int newWidth, int newHeight);
    void crop(Image& image, const GraphicMode& graphicMode);
    void cropFill(Image& image, const GraphicMode& graphicMode);
}

#endif // IMAGE_UTILS_H
