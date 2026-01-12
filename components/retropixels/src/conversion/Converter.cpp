#include "Converter.h"
#include <algorithm>

Converter::Converter(const Quantizer& quantizer) : quantizer(quantizer) {}

void Converter::convert(const IImageData& imageData, PixelImage& pixelImage) {
    std::vector<int> unrestrictedImage = quantizer.quantizeImage(imageData);

    for (size_t i = 0; i < pixelImage.colorMaps.size(); ++i) {
        extractColorMap(unrestrictedImage, pixelImage.colorMaps[i], static_cast<int>(i), pixelImage);
    }

    for (int y = 0; y < pixelImage.mode.height; ++y) {
        for (int x = 0; x < pixelImage.mode.width; ++x) {
            if (pixelImage.pixelIndex[y][x] == -1) {
                mapToExistingColorMap(pixelImage, x, y, peek(imageData, x, y));
            }
        }
    }
}

void Converter::mapToExistingColorMap(PixelImage& image, int x, int y, const std::vector<int>& realColor) {
    int closestMap = 0;
    double minDistance = std::numeric_limits<double>::max();

    for (size_t i = 0; i < image.colorMaps.size(); ++i) {
        auto paletteIndexOpt = image.colorMaps[i].get(x, y);
        if (paletteIndexOpt.has_value()) {
            int paletteIndex = paletteIndexOpt.value();
            if (paletteIndex != -1) {
                double distance = quantizer.distance(realColor, paletteIndex);
                if (distance < minDistance) {
                    minDistance = distance;
                    closestMap = static_cast<int>(i);
                }
            }
        }
    }
    image.pixelIndex[y][x] = closestMap;
}

int Converter::reduceToMax(const std::vector<int>& colors) {
    std::vector<int> weights(256, 0);
    int maxColor = 0, maxWeight = 0;

    for (int c : colors) {
        weights[c]++;
        if (weights[c] > maxWeight) {
            maxWeight = weights[c];
            maxColor = c;
        }
    }

    return maxColor;
}

void Converter::extractColorMap(std::vector<int>& quantizedImage, ColorMap& toColorMap, int colorMapIndex, PixelImage& pixelImage) {
    int imageWidth = pixelImage.mode.width;

    toColorMap.forEachCell([&](int x, int y) {
        std::vector<int> colorIndices;

        if (toColorMap.get(x, y) == -1) {
            toColorMap.forEachPixelInCell(x, y, [&](int xx, int yy) {
                int pixel = quantizedImage[xx + yy * imageWidth];
                if (pixel != -1) {
                    colorIndices.push_back(pixel);
                }
            });

            if (!colorIndices.empty()) {
                int winner = reduceToMax(colorIndices);
                toColorMap.put(x, y, winner);

                toColorMap.forEachPixelInCell(x, y, [&](int xx, int yy) {
                    if (quantizedImage[xx + yy * imageWidth] == winner) {
                        quantizedImage[xx + yy * imageWidth] = -1;
                        pixelImage.pixelIndex[yy][xx] = colorMapIndex;
                    }
                });
            }
        }
    });
}
