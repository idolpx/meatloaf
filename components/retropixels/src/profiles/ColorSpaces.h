#ifndef COLORSPACE_H
#define COLORSPACE_H

#include <vector>
#include <string>
#include <map>
#include <functional>

using Color = std::vector<double>;

class ColorSpace {
public:
    // Function mappings for different color spaces
    static std::map<std::string, std::function<Color(const Color&)>> ColorSpaces;

    // Color conversion functions
    static Color rgb(const Color& pixel);
    static Color rainbow(const Color& pixel);
    static Color yuv(const Color& pixel);
    static Color yCbCr(const Color& pixel);
    static Color xyz(const Color& pixel);
    static Color lab(const Color& pixel);
    static Color okLab(const Color& pixel);
};

#endif // COLORSPACE_H
