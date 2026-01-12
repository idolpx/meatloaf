#ifndef PIXELS_H
#define PIXELS_H

#include <vector>
#include <algorithm>
#include <stdexcept>
#include "../model/IImageData.h"

extern const std::vector<int> emptyPixel;

std::vector<int> add(const std::vector<int>& one, const std::vector<int>& other);
void poke(IImageData& imageData, int x, int y, const std::vector<int>& pixel);
std::vector<int> peekAtIndex(const IImageData& imageData, int index);
std::vector<int> peek(const IImageData& imageData, int x, int y);
int coordsToIndex(const IImageData& imageData, int x, int y);
int cap(int pixelChannel);

#endif // PIXELS_H
