#ifndef IMAGEEXPORTER_H
#define IMAGEEXPORTER_H

#include <vector>
#include <string>
#include "../model/ColorMap.h"
#include "../profiles/Palettes.h"

/**
 * Export a ColorMap to an image file (PNG format).
 * @param colorMap The ColorMap object to be exported.
 * @param filename The name of the output image file.
 * @param palette The color palette used for the image.
 */
void exportColorMap(ColorMap& colorMap, const std::string& filename, const Palette& palette);

/**
 * Export a quantized image to an image file (PNG format).
 * @param quantizedImage A vector representing the quantized image.
 * @param width The width of the image.
 * @param height The height of the image.
 * @param filename The name of the output image file.
 * @param palette The color palette used for the image.
 */
void exportQuantizedImage(const std::vector<int>& quantizedImage, int width, int height, const std::string& filename, const Palette& palette);

#endif // IMAGEEXPORTER_H
