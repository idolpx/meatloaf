#ifndef GRAPHICMODES_H
#define GRAPHICMODES_H

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <any>

#include "../model/PixelImage.h"
#include "../model/GraphicMode.h"

class GraphicModes {
public:
    static std::shared_ptr<PixelImage> bitmap(const std::map<std::string, std::any>& props);
    static std::shared_ptr<PixelImage> fli(const std::map<std::string, std::any>& props);
    static std::shared_ptr<PixelImage> sprites(const std::map<std::string, std::any>& props);

private:
    static std::shared_ptr<PixelImage> createMulticolorFLI();
    static std::shared_ptr<PixelImage> createHiresFLI();
};

#endif // GRAPHICMODES_H
