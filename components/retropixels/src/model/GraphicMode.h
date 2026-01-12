#ifndef GRAPHICMODE_H
#define GRAPHICMODE_H

#include <vector>
#include <map>
#include <functional>
#include <string>

class GraphicMode {
public:
    std::string id;
    int width;
    int height;
    int pixelWidth = 1;
    int pixelHeight = 1;
    int rowsPerCell = 8;
    int bytesPerCellRow = 1;
    int fliBugSize = 0;

    std::map<int, int> indexMap;

    GraphicMode(const std::string& id, int width, int height);

    int pixelsPerByte() const;
    int pixelsPerCellRow() const;

    void forEachCellRow(int cellY, const std::function<void(int)>& callback) const;
    void forEachCell(int yOffset, const std::function<void(int, int)>& callback) const;
    void forEachByte(int cellX, const std::function<void(int)>& callback) const;
    void forEachPixel(int byteX, const std::function<void(int, int)>& callback) const;
};

#endif // GRAPHICMODE_H
