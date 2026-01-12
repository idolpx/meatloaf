#include "Palette.h"

Palette::Palette(const std::vector<std::vector<int>>& colors) : colors(colors) {
    for (size_t i = 0; i < colors.size(); ++i) {
        enabled.push_back(i);
    }
}
