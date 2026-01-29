#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE2_IMPLEMENTATION
#include "stb_image_resize2.h"

#include "ImageUtils.h"
#include <iostream>
#include <algorithm>


namespace retropixels
{
    const std::vector<uint8_t> emptyPixel = {0, 0, 0, 255};

    // Load an image (supports PNG, BMP, JPG)
    Image readImage(const std::string& filename, GraphicMode& graphicMode, ScaleMode scaleMode) {
        int width, height, channels;
        uint8_t* data = stbi_load(filename.c_str(), &width, &height, &channels, 4); // Force 4 channels (RGBA)
        
        if (!data) {
            printf("Error: Could not read image file.\r\n");
            return Image(0, 0);
        }

        Image image(width, height);

        // Copy image data into our structure
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int index = (y * width + x) * 4;
                image.setPixel(x, y, {data[index], data[index + 1], data[index + 2], data[index + 3]});
            }
        }

        stbi_image_free(data); // Free memory

        // Apply scaling
        if (scaleMode == ScaleMode::NONE) {
            crop(image, graphicMode);
        } else if (scaleMode == ScaleMode::FILL) {
            cropFill(image, graphicMode);
        }

        return image;
    }


    // Save an image (supports PNG, JPEG, and GIF)
    // writeImage(pixelImage, "output.jpg", palette, "jpg", 80);  // JPEG (80% quality)
    // writeImage(pixelImage, "output.png", palette, "png");      // PNG (max compression)
    // writeImage(pixelImage, "output.gif", palette, "gif");      // GIF output
    bool writeImage(const PixelImage& pixelImage, const std::string& filename, const Palette& palette, const std::string& format, int quality) {
        Image image = toImage(pixelImage, palette);

        std::vector<uint8_t> rawData(image.width * image.height * 4);

        // Flatten image data
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                int index = (y * image.width + x) * 4;
                auto pixel = image.getPixel(x, y);
                rawData[index] = pixel[0];
                rawData[index + 1] = pixel[1];
                rawData[index + 2] = pixel[2];
                rawData[index + 3] = pixel[3];
            }
        }

        if (format == "jpg" || format == "jpeg") {
            // Save as JPEG with specified quality
            if (!stbi_write_jpg(filename.c_str(), image.width, image.height, 4, rawData.data(), quality)) {
                std::cerr << "Error: Failed to save image as JPG." << std::endl;
                return false;
            }
        } else if (format == "png") {
            // Save as PNG with optimized compression
            stbi_write_png_compression_level = 9;  // Maximum compression
            if (!stbi_write_png(filename.c_str(), image.width, image.height, 4, rawData.data(), image.width * 4)) {
                std::cerr << "Error: Failed to save image as PNG." << std::endl;
                return false;
            }
        } else if (format == "gif") {
            // Save as GIF (using RGB, ignoring alpha)
            std::vector<uint8_t> gifData(image.width * image.height * 3);
            for (size_t i = 0; i < rawData.size() / 4; i++) {
                gifData[i * 3 + 0] = rawData[i * 4 + 0];  // Red
                gifData[i * 3 + 1] = rawData[i * 4 + 1];  // Green
                gifData[i * 3 + 2] = rawData[i * 4 + 2];  // Blue
            }

            if (!stbi_write_bmp(filename.c_str(), image.width, image.height, 3, gifData.data())) {
                std::cerr << "Error: Failed to save image as GIF." << std::endl;
                return false;
            }
        } else {
            std::cerr << "Error: Unsupported image format." << std::endl;
            return false;
        }

        return true;
    }


    // Converts PixelImage to Image structure
    Image toImage(const PixelImage& pixelImage, const Palette& palette) {
        Image image(pixelImage.mode.width, pixelImage.mode.height);

        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                std::vector<uint8_t> pixelValue;

                if (x < pixelImage.mode.fliBugSize) {
                    pixelValue = {0, 0, 0, 255};
                } else {
                    int paletteIndex = pixelImage.peek(x, y);
                    if (paletteIndex == -1 || paletteIndex >= static_cast<int>(palette.colors.size())) {
                        pixelValue = emptyPixel;
                    } else {
                        pixelValue = {
                            static_cast<uint8_t>(palette.colors[paletteIndex][0]),
                            static_cast<uint8_t>(palette.colors[paletteIndex][1]),
                            static_cast<uint8_t>(palette.colors[paletteIndex][2]),
                            255
                        };
                    }
                }

                image.setPixel(x, y, pixelValue);
            }
        }
        return image;
    }

    // Bicubic interpolation helper - matches Jimp's implementation
    inline uint8_t interpolateCubic(uint8_t x0, uint8_t x1, uint8_t x2, uint8_t x3, double t) {
        double a0 = x3 - x2 - x0 + x1;
        double a1 = x0 - x1 - a0;
        double a2 = x2 - x0;
        double a3 = x1;
        double result = a0 * (t * t * t) + a1 * (t * t) + a2 * t + a3;
        return static_cast<uint8_t>(std::max(0.0, std::min(255.0, result)));
    }

    // Resize an image using Jimp's bicubic interpolation algorithm (memory-optimized)
    Image resizeImage(const Image& inputImage, int newWidth, int newHeight) {
        int wSrc = inputImage.width;
        int hSrc = inputImage.height;
        int wDst = newWidth;
        int hDst = newHeight;

        // Calculate intermediate dimensions
        int wM = std::max(1, wSrc / wDst);
        int wDst2 = wDst * wM;
        int hM = std::max(1, hSrc / hDst);
        int hDst2 = hDst * hM;

        // Use flat buffer for output instead of Image to save memory
        std::vector<uint8_t> outputBuffer(wDst * hDst * 4);

        // Keep only one row cache for horizontal interpolation
        std::vector<uint8_t> horzRow(wDst2 * 4);
        
        // Keep 4 rows for vertical interpolation window
        std::vector<uint8_t> vertWindow[4];
        for (int i = 0; i < 4; i++) {
            vertWindow[i].resize(wDst2 * 4);
        }
        int windowRow[4] = {-1, -1, -1, -1};
        int windowIdx = 0;
        
        // Helper to generate a horizontally-interpolated row
        auto generateHorzRow = [&](int srcY, std::vector<uint8_t>& dest) {
            for (int j = 0; j < wDst2; j++) {
                double x = j * (wSrc - 1.0) / wDst2;
                int xPos = static_cast<int>(std::floor(x));
                double t = x - xPos;
                
                auto p0 = inputImage.getPixel(std::max(0, xPos - 1), srcY);
                auto p1 = inputImage.getPixel(xPos, srcY);
                auto p2 = inputImage.getPixel(std::min(wSrc - 1, xPos + 1), srcY);
                auto p3 = inputImage.getPixel(std::min(wSrc - 1, xPos + 2), srcY);
                
                for (int k = 0; k < 4; k++) {
                    dest[j * 4 + k] = interpolateCubic(p0[k], p1[k], p2[k], p3[k], t);
                }
            }
        };
        
        // Helper to ensure a row is in the vertical window
        auto ensureRowInWindow = [&](int srcY) -> int {
            // Check if already in window
            for (int i = 0; i < 4; i++) {
                if (windowRow[i] == srcY) return i;
            }
            // Generate and add to window (LRU replacement)
            int idx = windowIdx;
            generateHorzRow(srcY, vertWindow[idx]);
            windowRow[idx] = srcY;
            windowIdx = (windowIdx + 1) % 4;
            return idx;
        };
        
        // Process each block for final output
        for (int outY = 0; outY < hDst; outY++) {
            for (int outX = 0; outX < wDst; outX++) {
                int r = 0, g = 0, b = 0, a = 0;
                int pixelCount = 0;
                
                // Average over the block
                for (int dy = 0; dy < hM; dy++) {
                    int srcRow = outY * hM + dy;
                    if (srcRow >= hDst2) continue;
                    
                    double yy = srcRow * (hSrc - 1.0) / hDst2;
                    int yyPos = static_cast<int>(std::floor(yy));
                    double tt = yy - yyPos;
                    
                    // Ensure we have the 4 rows we need for vertical interpolation
                    int idx0 = ensureRowInWindow(std::max(0, yyPos - 1));
                    int idx1 = ensureRowInWindow(yyPos);
                    int idx2 = ensureRowInWindow(std::min(hSrc - 1, yyPos + 1));
                    int idx3 = ensureRowInWindow(std::min(hSrc - 1, yyPos + 2));
                    
                    for (int dx = 0; dx < wM; dx++) {
                        int srcCol = outX * wM + dx;
                        if (srcCol >= wDst2) continue;
                        
                        // Vertical interpolation
                        int pos = srcCol * 4;
                        for (int k = 0; k < 4; k++) {
                            int y0 = vertWindow[idx0][pos + k];
                            int y1 = vertWindow[idx1][pos + k];
                            int y2 = vertWindow[idx2][pos + k];
                            int y3 = vertWindow[idx3][pos + k];
                            
                            int val = interpolateCubic(y0, y1, y2, y3, tt);
                            
                            if (k == 0) r += val;
                            else if (k == 1) g += val;
                            else if (k == 2) b += val;
                            else a += val;
                        }
                        pixelCount++;
                    }
                }
                
                // Set the output pixel in flat buffer
                if (pixelCount > 0) {
                    int outIdx = (outY * wDst + outX) * 4;
                    outputBuffer[outIdx] = r / pixelCount;
                    outputBuffer[outIdx + 1] = g / pixelCount;
                    outputBuffer[outIdx + 2] = b / pixelCount;
                    outputBuffer[outIdx + 3] = a / pixelCount;
                }
            }
        }

        // Create output image and populate pixel-by-pixel to avoid memory spike
        Image outputImage(wDst, hDst);
        for (int y = 0; y < hDst; ++y) {
            for (int x = 0; x < wDst; ++x) {
                int idx = (y * wDst + x) * 4;
                outputImage.setPixel(x, y, {outputBuffer[idx], outputBuffer[idx + 1], outputBuffer[idx + 2], outputBuffer[idx + 3]});
            }
        }

        return outputImage;
    }

    // Resize using nearest neighbor (for pixel-perfect downscaling)
    Image resizeImageNearestNeighbor(const Image& inputImage, int newWidth, int newHeight) {
        Image outputImage(newWidth, newHeight);
        
        double xRatio = static_cast<double>(inputImage.width) / newWidth;
        double yRatio = static_cast<double>(inputImage.height) / newHeight;
        
        for (int y = 0; y < newHeight; ++y) {
            for (int x = 0; x < newWidth; ++x) {
                int srcX = static_cast<int>(x * xRatio);
                int srcY = static_cast<int>(y * yRatio);
                outputImage.setPixel(x, y, inputImage.getPixel(srcX, srcY));
            }
        }
        
        return outputImage;
    }

    // Cropping function
    void crop(Image& image, const GraphicMode& graphicMode) {
        if (image.width < graphicMode.width || image.height < graphicMode.height) {
            printf("Error: Image size is too small for the graphic mode.\r\n");
            return;
        }

        int targetWidth = graphicMode.width;
        int targetHeight = graphicMode.height;
        
        if (graphicMode.pixelWidth != 1) {
            targetWidth = image.width / graphicMode.pixelWidth;
        }

        // Crop by copying relevant portion to new buffer
        std::vector<uint8_t> newData(targetWidth * targetHeight * 4);
        for (int y = 0; y < targetHeight && y < image.height; ++y) {
            for (int x = 0; x < targetWidth && x < image.width; ++x) {
                int srcIdx = (y * image.width + x) * 4;
                int dstIdx = (y * targetWidth + x) * 4;
                newData[dstIdx] = image.data[srcIdx];
                newData[dstIdx + 1] = image.data[srcIdx + 1];
                newData[dstIdx + 2] = image.data[srcIdx + 2];
                newData[dstIdx + 3] = image.data[srcIdx + 3];
            }
        }
        
        image.width = targetWidth;
        image.height = targetHeight;
        image.data = std::move(newData);
    }

    // Resizing function to fill the target dimensions
    void cropFill(Image& image, const GraphicMode& graphicMode) {
        int targetWidth = graphicMode.width * graphicMode.pixelWidth;
        int targetHeight = graphicMode.height * graphicMode.pixelHeight;
        
        // Calculate scaling to cover the target area (like jimp.cover())
        double scaleX = static_cast<double>(targetWidth) / image.width;
        double scaleY = static_cast<double>(targetHeight) / image.height;
        double scale = std::max(scaleX, scaleY); // Use larger scale to cover
        
        int scaledWidth = static_cast<int>(image.width * scale);
        int scaledHeight = static_cast<int>(image.height * scale);
        
        // Resize to cover the target
        Image resized = resizeImage(image, scaledWidth, scaledHeight);
        
        // Crop from center to exact target size
        int offsetX = (scaledWidth - targetWidth) / 2;
        int offsetY = (scaledHeight - targetHeight) / 2;
        
        Image cropped(targetWidth, targetHeight);
        for (int y = 0; y < targetHeight; ++y) {
            for (int x = 0; x < targetWidth; ++x) {
                int srcX = x + offsetX;
                int srcY = y + offsetY;
                if (srcX >= 0 && srcX < scaledWidth && srcY >= 0 && srcY < scaledHeight) {
                    cropped.setPixel(x, y, resized.getPixel(srcX, srcY));
                }
            }
        }
        
        // Now resize down to final dimensions (using bilinear to match Node's Jimp default)
        Image final = resizeImage(cropped, graphicMode.width, graphicMode.height);
        
        image.width = final.width;
        image.height = final.height;
        image.data = std::move(final.data);
    }
}