#include "GraphicMode.h"

GraphicMode::GraphicMode(const std::string& id, int width, int height)
    : id(id), width(width), height(height), indexMap({{0, 0}, {1, 1}, {2, 2}, {3, 3}}) {}

int GraphicMode::pixelsPerByte() const {
    return 8 / pixelWidth;
}

int GraphicMode::pixelsPerCellRow() const {
    return bytesPerCellRow * pixelsPerByte();
}

void GraphicMode::forEachCellRow(int cellY, const std::function<void(int)>& callback) const {
    for (int rowY = cellY; rowY < cellY + rowsPerCell; ++rowY) {
        callback(rowY);
    }
}

void GraphicMode::forEachCell(int yOffset, const std::function<void(int, int)>& callback) const {
    const int pixelsPerCellRow = this->pixelsPerCellRow();
    for (int y = yOffset; y < height; y += rowsPerCell) {
        for (int x = 0; x < width; x += pixelsPerCellRow) {
            callback(x, y);
        }
    }
}

void GraphicMode::forEachByte(int cellX, const std::function<void(int)>& callback) const {
    const int pixelsPerByte = this->pixelsPerByte();
    for (int byteX = cellX; byteX < cellX + bytesPerCellRow * pixelsPerByte; byteX += pixelsPerByte) {
        callback(byteX);
    }
}

void GraphicMode::forEachPixel(int byteX, const std::function<void(int, int)>& callback) const {
    const int pixelsPerByte = this->pixelsPerByte();
    for (int pixelX = 0; pixelX < pixelsPerByte; ++pixelX) {
        const int shiftTimes = (pixelsPerByte - 1 - pixelX) * pixelWidth;
        callback(byteX + pixelX, shiftTimes);
    }
}
