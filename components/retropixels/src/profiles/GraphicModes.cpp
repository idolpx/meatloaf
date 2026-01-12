#include "GraphicModes.h"

// Bitmap mode
std::shared_ptr<PixelImage> GraphicModes::bitmap(const std::map<std::string, std::any>& props) {
    bool isHires = std::any_cast<bool>(props.at("hires"));
    bool isNoMaps = std::any_cast<bool>(props.at("nomaps"));
    int width = isHires ? 320 : 160;

    auto gm = std::make_shared<GraphicMode>("bitmap", width, 200);
    gm->pixelWidth = isHires ? 1 : 2;

    auto result = std::make_shared<PixelImage>(*gm);
    if (!isHires) {
        result->addColorMap();
    }
    if (isNoMaps) {
        result->addColorMap();
        result->addColorMap();
        if (!isHires) {
            result->addColorMap();
        }
    } else {
        result->addColorMap(gm->pixelsPerByte(), gm->rowsPerCell);
        result->addColorMap(gm->pixelsPerByte(), gm->rowsPerCell);
        if (!isHires) {
            result->addColorMap(gm->pixelsPerByte(), gm->rowsPerCell);
        }
    }
    return result;
}

// Multicolor FLI
std::shared_ptr<PixelImage> GraphicModes::createMulticolorFLI() {
    auto gm = std::make_shared<GraphicMode>("fli", 160, 200);
    gm->pixelWidth = 2;
    gm->fliBugSize = 3 * 4;
    gm->indexMap = {{0, 0}, {1, 3}, {2, 2}, {3, 1}};
    
    auto result = std::make_shared<PixelImage>(*gm);
    result->addColorMap();
    result->addColorMap(4, 8);
    result->addColorMap(4, 1);
    result->addColorMap(4, 1);
    return result;
}

// Hires FLI
std::shared_ptr<PixelImage> GraphicModes::createHiresFLI() {
    auto gm = std::make_shared<GraphicMode>("fli", 320, 200);
    gm->fliBugSize = 3 * 8;
    
    auto result = std::make_shared<PixelImage>(*gm);
    result->addColorMap(8, 1);
    result->addColorMap(8, 1);
    return result;
}

// FLI mode (Hires or Multicolor)
std::shared_ptr<PixelImage> GraphicModes::fli(const std::map<std::string, std::any>& props) {
    return std::any_cast<bool>(props.at("hires")) ? createHiresFLI() : createMulticolorFLI();
}

// Sprites mode
std::shared_ptr<PixelImage> GraphicModes::sprites(const std::map<std::string, std::any>& props) {
    int nrRows = std::any_cast<int>(props.at("rows"));
    int nrColumns = std::any_cast<int>(props.at("columns"));
    bool isHires = std::any_cast<bool>(props.at("hires"));
    bool isNoMaps = std::any_cast<bool>(props.at("nomaps"));

    int pixelsPerColumn = isHires ? 24 : 12;
    int height = nrRows * 21;
    int width = nrColumns * pixelsPerColumn;

    auto gm = std::make_shared<GraphicMode>("sprites", width, height);
    gm->pixelWidth = isHires ? 1 : 2;
    gm->bytesPerCellRow = 3;
    gm->rowsPerCell = 21;
    gm->indexMap = {{0, 0}, {1, 1}, {2, 3}, {3, 2}};
    
    auto result = std::make_shared<PixelImage>(*gm);
    result->addColorMap();
    if (!isHires) {
        result->addColorMap();
        result->addColorMap();
    }
    if (isNoMaps) {
        result->addColorMap();
    } else {
        result->addColorMap(pixelsPerColumn, 21);
    }
    return result;
}
