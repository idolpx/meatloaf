#ifndef IBINARYFORMAT_H
#define IBINARYFORMAT_H

#include <string>
#include <vector>
#include "../model/PixelImage.h" // Assuming PixelImage is defined elsewhere

/**
 * Interface for binary image formats.
 */
class IBinaryFormat {
public:
    virtual ~IBinaryFormat() = default; // Ensures proper destruction of derived classes

    /**
     * Get the format name.
     * @return The name of the binary format.
     */
    virtual std::string getFormatName() const = 0;

    /**
     * Get the default file extension for the format.
     * @return The default extension string.
     */
    virtual std::string getDefaultExtension() const = 0;

    /**
     * Convert a PixelImage to this binary format.
     * @param pixelImage The input image.
     */
    virtual void fromPixelImage(const PixelImage& pixelImage) = 0;

    /**
     * Convert the binary format to a memory-mapped sequence of byte vectors.
     * @return A vector of byte vectors representing the memory map.
     */
    virtual std::vector<std::vector<uint8_t>> toMemoryMap() const = 0;
};

#endif // IBINARYFORMAT_H
