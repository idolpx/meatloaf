#include "ColorSpaces.h"
#include <cmath>

// Initialize the ColorSpaces map
std::map<std::string, std::function<Color(const Color&)>> ColorSpace::ColorSpaces = {
    {"rgb", ColorSpace::rgb},
    {"yuv", ColorSpace::yuv},
    {"rainbow", ColorSpace::rainbow},
    {"ycbcr", ColorSpace::yCbCr},
    {"xyz", ColorSpace::xyz},
    {"lab", ColorSpace::lab},
    {"oklab", ColorSpace::okLab}
};

// RGB (No transformation)
Color ColorSpace::rgb(const Color& pixel) {
    return pixel;
}

// Rainbow effect: Converts to YUV and zeroes out U and V
Color ColorSpace::rainbow(const Color& pixel) {
    Color yuvPixel = ColorSpace::yuv(pixel);
    return {yuvPixel[0], 0, 0};
}

// YUV conversion (SDTV with BT.602 standard)
Color ColorSpace::yuv(const Color& pixel) {
    return {
        pixel[0] * 0.299 + pixel[1] * 0.587 + pixel[2] * 0.114,
        pixel[0] * -0.14713 + pixel[1] * -0.28886 + pixel[2] * 0.436,
        pixel[0] * 0.615 + pixel[1] * -0.51499 + pixel[2] * -0.10001
    };
}

// YCbCr conversion
Color ColorSpace::yCbCr(const Color& pixel) {
    return {
        0.299 * pixel[0] + 0.587 * pixel[1] + 0.114 * pixel[2],
        -0.16874 * pixel[0] - 0.33126 * pixel[1] + 0.5 * pixel[2],
        0.5 * pixel[0] - 0.41869 * pixel[1] - 0.08131 * pixel[2]
    };
}

// XYZ conversion (sRGB-based)
Color ColorSpace::xyz(const Color& pixel) {
    double r = pixel[0], g = pixel[1], b = pixel[2];

    r = r > 0.04045 ? std::pow((r + 0.055) / 1.055, 2.4) : r / 12.92;
    g = g > 0.04045 ? std::pow((g + 0.055) / 1.055, 2.4) : g / 12.92;
    b = b > 0.04045 ? std::pow((b + 0.055) / 1.055, 2.4) : b / 12.92;

    return {
        r * 41.24 + g * 35.76 + b * 18.05,
        r * 21.26 + g * 71.52 + b * 7.22,
        r * 1.93 + g * 11.92 + b * 95.05
    };
}

// Lab conversion (using XYZ and reference white point)
Color ColorSpace::lab(const Color& pixel) {
    Color xyzPixel = ColorSpace::xyz(pixel);
    const double refX = 95.047, refY = 100.0, refZ = 108.883;
    const double epsilon = 0.008856, kappa = 7.787, bla = 16.0 / 116.0;
    
    double x = xyzPixel[0] / refX;
    double y = xyzPixel[1] / refY;
    double z = xyzPixel[2] / refZ;

    x = x > epsilon ? std::pow(x, 1.0 / 3.0) : (kappa * x + bla);
    y = y > epsilon ? std::pow(y, 1.0 / 3.0) : (kappa * y + bla);
    z = z > epsilon ? std::pow(z, 1.0 / 3.0) : (kappa * z + bla);

    return {
        116 * y - 16,
        500 * (x - y),
        200 * (y - z)
    };
}

// okLab conversion (modern perceptual color space)
Color ColorSpace::okLab(const Color& pixel) {
    double r = pixel[0], g = pixel[1], b = pixel[2];

    double l = 0.412165612 * r + 0.536275208 * g + 0.0514575653 * b;
    double m = 0.211859107 * r + 0.6807189584 * g + 0.107406579 * b;
    double s = 0.0883097947 * r + 0.2818474174 * g + 0.6302613616 * b;

    return {
        0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s,
        1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
        0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s
    };
}
