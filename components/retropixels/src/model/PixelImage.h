#ifndef PIXELIMAGE_H
#define PIXELIMAGE_H

#include <vector>
#include <optional>
#include <array>
#include <algorithm>
#include <functional>
#include <stdexcept>

#include "GraphicMode.h"
#include "ColorMap.h"

class PixelImage {
public:
    std::vector<ColorMap> colorMaps;
    GraphicMode mode;
    std::vector<std::vector<int>> pixelIndex;

    explicit PixelImage(const GraphicMode& mode);

    int peek(int x, int y) const;
    void addColorMap(int resXVal = -1, int resYVal = -1);
    int mapPixelIndex(int x, int y) const;
    std::vector<uint8_t> extractAttributeData(int yOffset, const std::function<int(int, int)>& callback) const;
    bool isHires() const;
    std::vector<PixelImage> debugColorMaps() const;
};

#endif // PIXELIMAGE_H
