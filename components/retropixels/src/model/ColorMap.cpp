#include "ColorMap.h"

ColorMap::ColorMap(int widthVal, int heightVal, int resXVal, int resYVal)
    : colors(widthVal * heightVal, std::vector<int>(heightVal, -1)),
      width(widthVal),
      height(heightVal),
      resX(resXVal ? resXVal : widthVal),
      resY(resYVal ? resYVal : heightVal) {}

void ColorMap::put(int x, int y, int paletteIndex) {
    if (!isInRange(x, y)) {
        return;
    }

    const int rx = mapX(x);
    if (colors[rx].empty()) {
        colors[rx].resize(height, -1); // Initialize row if empty
    }
    colors[rx][mapY(y)] = paletteIndex;
}

std::optional<int> ColorMap::get(int x, int y) const {
    const int mX = mapX(x);
    if (mX >= 0 && mX < colors.size() && !colors[mX].empty()) {
        return colors[mX][mapY(y)];
    }
    return std::nullopt;
}

int ColorMap::getNonEmpty(int x, int y) const {
    const auto result = get(x, y);
    if (!result) {
        throw std::out_of_range("Index at " + std::to_string(x) + ", " + std::to_string(y) + " is undefined.");
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
