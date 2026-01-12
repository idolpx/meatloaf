#ifndef BINARYFORMATCONVERTER_H
#define BINARYFORMATCONVERTER_H

#include <memory>
#include <vector>
#include "../model/PixelImage.h"
#include "IBinaryFormat.h"

/**
 * Convert a PixelImage to an IBinaryFormat object.
 */
std::unique_ptr<IBinaryFormat> toBinary(const PixelImage& pixelImage);

/**
 * Convert an IBinaryFormat object to a byte buffer.
 */
std::vector<uint8_t> toBuffer(const IBinaryFormat& image);

#endif // BINARYFORMATCONVERTER_H
