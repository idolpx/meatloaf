#include "ColorMap.h"

ColorMap::ColorMap(int widthVal, int heightVal, int resXVal, int resYVal)
    : width(widthVal),
      height(heightVal),
      resX(resXVal ? resXVal : widthVal),
      resY(resYVal ? resYVal : heightVal)
{
    // Calculate mapped dimensions (number of cells, not pixels)
    int mappedWidth = (widthVal + resX - 1) / resX;  // Ceiling division
    int mappedHeight = (heightVal + resY - 1) / resY; // Ceiling division
    
    // Allocate only the mapped dimensions, not full pixel dimensions
    // This is a sparse array - we only allocate rows as needed
    colors.resize(mappedWidth);
    // Don't pre-allocate columns, they're created on-demand in put()
}

void ColorMap::put(int x, int y, int paletteIndex) {
    if (!isInRange(x, y)) {
        return;
    }

    const int rx = mapX(x);
    const int ry = mapY(y);
    
    // Calculate mapped height for proper column allocation
    int mappedHeight = (height + resY - 1) / resY;
    
    if (colors[rx].empty()) {
        colors[rx].resize(mappedHeight, -1); // Initialize row with mapped height
    }
    colors[rx][ry] = paletteIndex;
}

std::optional<int> ColorMap::get(int x, int y) const {
    const int mX = mapX(x);
    const int mY = mapY(y);
    
    if (mX >= 0 && mX < colors.size() && !colors[mX].empty()) {
        if (mY >= 0 && mY < colors[mX].size()) {
            int value = colors[mX][mY];
            if (value != -1) {  // Check if the value has been set
                return value;
            }
        }
    }
    return std::nullopt;
}

int ColorMap::getNonEmpty(int x, int y) const {
    const auto result = get(x, y);
    if (!result) {
        printf("Index at %d, %d is undefined.\r\n", x, y);
        return 0;
    }
    return *result;
}

int ColorMap::getIndexOrDefault(int x, int y) const {
    return get(x, y).value_or(0);
}

void ColorMap::forEachCell(const std::function<void(int, int)>& callback) const {
    for (int x = 0; x < width; x += resX) {
        for (int y = 0; y < height; y += resY) {
            callback(x, y);
        }
    }
}

void ColorMap::forEachPixelInCell(int x, int y, const std::function<void(int, int)>& callback) const {
    for (int ix = x; ix < x + resX && ix < width; ++ix) {
        for (int iy = y; iy < y + resY && iy < height; ++iy) {
            callback(ix, iy);
        }
    }
}

bool ColorMap::isInRange(int x, int y) const {
    return x >= 0 && x < width && y >= 0 && y < height;
}

int ColorMap::mapX(int x) const {
    return std::floor(x / static_cast<double>(resX));
}

int ColorMap::mapY(int y) const {
    return std::floor(y / static_cast<double>(resY));
}
