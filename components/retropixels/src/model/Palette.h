#ifndef PALETTE_H
#define PALETTE_H

#include <vector>

class Palette {
public:
    std::vector<std::vector<int>> colors;
    std::vector<int> enabled;

    explicit Palette(const std::vector<std::vector<int>>& colors);
};

#endif // PALETTE_H
