#ifndef CONVERTER_H
#define CONVERTER_H

#include <vector>
#include <limits>
#include "Quantizer.h"
#include "../model/IImageData.h"
#include "../model/PixelImage.h"
#include "../model/ColorMap.h"
#include "../model/Pixels.h"

class Converter {
  private:
      Quantizer quantizer;

      void mapToExistingColorMap(PixelImage& image, int x, int y, const std::vector<int>& realColor);
      static int reduceToMax(const std::vector<int>& colors);
      static void extractColorMap(std::vector<int>& quantizedImage, ColorMap& toColorMap, int colorMapIndex, PixelImage& pixelImage);

  public:
      explicit Converter(const Quantizer& quantizer);
      void convert(const IImageData& imageData, PixelImage& pixelImage);
};

#endif // CONVERTER_H
