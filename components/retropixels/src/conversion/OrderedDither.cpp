#include "OrderedDither.h"
#include <limits>
#include <string>
#include "../model/Pixels.h"

const std::map<std::string, std::vector<std::vector<int>>> OrderedDither::presets = {
    {"bayer2x2", {{1, 3}, {4, 2}}},
    {"bayer4x4", {
        {1, 9, 3, 11}, {13, 5, 15, 7}, {4, 12, 2, 10}, {16, 8, 14, 6}
    }},
    {"bayer8x8", {
        {1, 49, 13, 61, 4, 52, 16, 64},
        {33, 17, 45, 29, 36, 20, 48, 31},
        {9, 57, 5, 53, 12, 60, 8, 56},
        {41, 25, 37, 21, 44, 28, 40, 24},
        {3, 51, 15, 63, 2, 50, 14, 62},
        {35, 19, 47, 31, 34, 18, 46, 30},
        {11, 59, 7, 55, 10, 58, 6, 54},
        {43, 27, 39, 23, 42, 26, 38, 22}
    }},
    {"none", {{0}}},
    {"test", {{1, 2, 3, 4}, {12, 13, 14, 5}, {11, 16, 15, 6}, {10, 9, 8, 7}}},
    {"test2", {{1, 5, 6, 2}, {9, 13, 14, 11}, {10, 16, 15, 12}, {3, 7, 8, 4}}},
    {"test3", {
        {1, 2, 3, 4, 33, 34, 35, 36},
        {5, 6, 7, 8, 37, 38, 39, 40},
        {9, 10, 11, 12, 41, 42, 43, 44},
        {13, 14, 15, 16, 45, 46, 47, 48},
        {49, 50, 51, 52, 17, 18, 19, 20},
        {53, 54, 55, 56, 21, 22, 23, 24},
        {57, 58, 59, 60, 25, 26, 27, 28},
        {61, 62, 63, 64, 29, 30, 31, 32}
    }}
};

OrderedDither::OrderedDither(const std::vector<std::vector<int>>& normalizedMatrix, int depth) {
    const double factor = 1.0 / (normalizedMatrix.size() * normalizedMatrix[0].size());
    matrix = normalizedMatrix;

    for (size_t i = 0; i < matrix.size(); ++i) {
        for (size_t j = 0; j < matrix[i].size(); ++j) {
            matrix[i][j] = depth * (factor * matrix[i][j] - 0.5);
        }
    }
}

void OrderedDither::dither(IImageData& image) {
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            poke(image, x, y, offsetColor(peek(image, x, y), x, y));
        }
    }
}

std::vector<int> OrderedDither::offsetColor(const std::vector<int>& color, int x, int y) const {
    const int offset = matrix[y % matrix.size()][x % matrix[0].size()];
    return add(color, {offset, offset, offset});
}
