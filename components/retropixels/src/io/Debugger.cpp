#include "Debugger.h"

#include "../model/Pixels.h"
#include "stb_image_write.h"
#include <vector>
#include <cstdint>

void exportColorMap(ColorMap& colorMap, const std::string& filename, const Palette& palette) {
    std::vector<uint8_t> image(colorMap.getWidth() * colorMap.getHeight() * 4, 255);

    colorMap.forEachCell([&](int x, int y) {
        colorMap.forEachPixelInCell(x, y, [&](int xx, int yy) {
            int index = colorMap.getNonEmpty(xx, yy);
            // poke expects IImageData, need to use manual assignment
            int pos = (yy * colorMap.getWidth() + xx) * 4;
            const std::vector<int>& color = palette.colors[index];
            image[pos] = color[0];
            image[pos + 1] = color[1];
            image[pos + 2] = color[2];
            image[pos + 3] = color.size() > 3 ? color[3] : 255;
        });
    });

    stbi_write_png(filename.c_str(), colorMap.getWidth(), colorMap.getHeight(), 4, image.data(), colorMap.getWidth() * 4);
}

void exportQuantizedImage(const std::vector<int>& quantizedImage, int width, int height, const std::string& filename, const Palette& palette) {
    std::vector<uint8_t> image(width * height * 4, 255);

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            int index = quantizedImage[x + y * width];
            int pos = (y * width + x) * 4;
            const std::vector<int>& color = palette.colors[index];
            image[pos] = color[0];
            image[pos + 1] = color[1];
            image[pos + 2] = color[2];
            image[pos + 3] = color.size() > 3 ? color[3] : 255;
        }
    }

    stbi_write_png(filename.c_str(), width, height, 4, image.data(), width * 4);
}
