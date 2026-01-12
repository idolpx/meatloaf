#ifndef COLORMAP_H
#define COLORMAP_H

#include <vector>
#include <optional>
#include <string>
#include <functional>
#include <cmath>
#include <stdexcept>

class ColorMap {
public:
    ColorMap(int widthVal, int heightVal, int resXVal = 0, int resYVal = 0);

    void put(int x, int y, int paletteIndex);
    std::optional<int> get(int x, int y) const;
    int getNonEmpty(int x, int y) const;
    int getIndexOrDefault(int x, int y) const;

    void forEachCell(const std::function<void(int, int)>& callback) const;
    void forEachPixelInCell(int x, int y, const std::function<void(int, int)>& callback) const;
    
    // Getters
    int getWidth() const { return width; }
    int getHeight() const { return height; }

private:
    std::vector<std::vector<int>> colors;
    int width;
    int height;
    int resX;
    int resY;

    bool isInRange(int x, int y) const;
    int mapX(int x) const;
    int mapY(int y) const;
};

#endif // COLORMAP_H
