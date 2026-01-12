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
            throw std::runtime_error("Error: Could not read image file.");
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

    // Resize an image using Jimp's bicubic interpolation algorithm
    Image resizeImage(const Image& inputImage, int newWidth, int newHeight) {
        int wSrc = inputImage.width;
        int hSrc = inputImage.height;
        int wDst = newWidth;
        int hDst = newHeight;

        // Flatten source data
        std::vector<uint8_t> bufSrc(wSrc * hSrc * 4);
        for (int y = 0; y < hSrc; ++y) {
            for (int x = 0; x < wSrc; ++x) {
                int index = (y * wSrc + x) * 4;
                auto pixel = inputImage.getPixel(x, y);
                bufSrc[index] = pixel[0];
                bufSrc[index + 1] = pixel[1];
                bufSrc[index + 2] = pixel[2];
                bufSrc[index + 3] = pixel[3];
            }
        }

        // Calculate intermediate dimensions
        int wM = std::max(1, wSrc / wDst);
        int wDst2 = wDst * wM;
        int hM = std::max(1, hSrc / hDst);
        int hDst2 = hDst * hM;

        // Pass 1 - interpolate rows (horizontal pass)
        std::vector<uint8_t> buf1(wDst2 * hSrc * 4);
        
        for (int i = 0; i < hSrc; i++) {
            for (int j = 0; j < wDst2; j++) {
                double x = j * (wSrc - 1.0) / wDst2;
                int xPos = static_cast<int>(std::floor(x));
                double t = x - xPos;
                int srcPos = (i * wSrc + xPos) * 4;
                int buf1Pos = (i * wDst2 + j) * 4;

                for (int k = 0; k < 4; k++) {
                    int kPos = srcPos + k;
                    int x0 = xPos > 0 ? bufSrc[kPos - 4] : 2 * bufSrc[kPos] - bufSrc[kPos + 4];
                    int x1 = bufSrc[kPos];
                    int x2 = bufSrc[kPos + 4];
                    int x3 = xPos < wSrc - 2 ? bufSrc[kPos + 8] : 2 * bufSrc[kPos + 4] - bufSrc[kPos];
                    buf1[buf1Pos + k] = interpolateCubic(std::max(0, std::min(255, x0)), 
                                                          x1, x2, 
                                                          std::max(0, std::min(255, x3)), t);
                }
            }
        }

        // Pass 2 - interpolate columns (vertical pass)
        std::vector<uint8_t> buf2(wDst2 * hDst2 * 4);
        
        for (int i = 0; i < hDst2; i++) {
            for (int j = 0; j < wDst2; j++) {
                double y = i * (hSrc - 1.0) / hDst2;
                int yPos = static_cast<int>(std::floor(y));
                double t = y - yPos;
                int buf1Pos = (yPos * wDst2 + j) * 4;
                int buf2Pos = (i * wDst2 + j) * 4;

                for (int k = 0; k < 4; k++) {
                    int kPos = buf1Pos + k;
                    int y0 = yPos > 0 ? buf1[kPos - wDst2 * 4] : 2 * buf1[kPos] - buf1[kPos + wDst2 * 4];
                    int y1 = buf1[kPos];
                    int y2 = buf1[kPos + wDst2 * 4];
                    int y3 = yPos < hSrc - 2 ? buf1[kPos + wDst2 * 8] : 2 * buf1[kPos + wDst2 * 4] - buf1[kPos];
                    buf2[buf2Pos + k] = interpolateCubic(std::max(0, std::min(255, y0)), 
                                                          y1, y2, 
                                                          std::max(0, std::min(255, y3)), t);
                }
            }
        }

        // Pass 3 - scale to final destination (box filter if needed)
        std::vector<uint8_t> bufDst(wDst * hDst * 4);
        int m = wM * hM;
        
        if (m > 1) {
            // Average pixels in blocks
            for (int i = 0; i < hDst; i++) {
                for (int j = 0; j < wDst; j++) {
                    int r = 0, g = 0, b = 0, a = 0;
                    
                    for (int y = 0; y < hM; y++) {
                        for (int x = 0; x < wM; x++) {
                            int idx = ((i * hM + y) * wDst2 + (j * wM + x)) * 4;
                            r += buf2[idx];
                            g += buf2[idx + 1];
                            b += buf2[idx + 2];
                            a += buf2[idx + 3];
                        }
                    }
                    
                    int dstPos = (i * wDst + j) * 4;
                    bufDst[dstPos] = r / m;
                    bufDst[dstPos + 1] = g / m;
                    bufDst[dstPos + 2] = b / m;
                    bufDst[dstPos + 3] = a / m;
                }
            }
        } else {
            // Direct copy
            bufDst = buf2;
        }

        // Create output image
        Image outputImage(wDst, hDst);
        for (int y = 0; y < hDst; ++y) {
            for (int x = 0; x < wDst; ++x) {
                int index = (y * wDst + x) * 4;
                outputImage.setPixel(x, y, {bufDst[index], bufDst[index + 1], bufDst[index + 2], bufDst[index + 3]});
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
            throw std::runtime_error("Error: Image size is too small for the graphic mode.");
        }

        if (graphicMode.pixelWidth != 1) {
            int newWidth = image.width / graphicMode.pixelWidth;
            image.width = newWidth;
        }

        image.pixels.resize(graphicMode.height);
        for (auto& row : image.pixels) {
            row.resize(graphicMode.width);
        }
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
        image.pixels = std::move(final.pixels);
        image.data = std::move(final.data);
    }
}