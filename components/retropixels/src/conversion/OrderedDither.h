#ifndef ORDERED_DITHER_H
#define ORDERED_DITHER_H

#include <vector>
#include <map>
#include "../model/IImageData.h"

class OrderedDither {
public:
    static const std::map<std::string, std::vector<std::vector<int>>> presets;

private:
    std::vector<std::vector<int>> matrix;

public:
    OrderedDither(const std::vector<std::vector<int>>& normalizedMatrix, int depth);
    void dither(IImageData& image);

private:
    std::vector<int> offsetColor(const std::vector<int>& color, int x, int y) const;
};

#endif // ORDERED_DITHER_H
