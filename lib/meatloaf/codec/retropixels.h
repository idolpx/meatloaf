// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

// RetroPixels - Image to C64 format converter
// Wrapper for RetroPixels component
// URL format: retropixels:<source_url>#format=koala&palette=colodore&dither=bayer4x4&scale=fill
// Example: retropixels:http://server/image.jpg?size=large#format=koala&palette=colodore
// PRG format: retropixels:http://server/image.jpg#format=prg (creates executable with viewer)
// PRG with base: retropixels:http://server/image.jpg#format=prg&baseformat=fli&palette=pepto

#ifndef MEATLOAF_CODEC_RETROPIXELS
#define MEATLOAF_CODEC_RETROPIXELS

#include <string>
#include <vector>
#include <memory>
#include "meatloaf.h"

// Forward declarations for RetroPixels types
namespace retropixels {
    enum class ScaleMode;
}
class IImageData;
class PixelImage;
class GraphicMode;
class Palette;
class Quantizer;
class Converter;

/********************************************************
 * RetroPixels Format Types
 ********************************************************/
enum class RetroPixelsFormat {
    KOALA,        // .kla - Koala Painter
    ARTSTUDIO,    // .art - Art Studio
    FLI,          // .fli - FLI Picture
    AFLI,         // .afli - AFLI Picture
    SPRITEPAD,    // .spd - SpritePad
    PRG           // .prg - Executable with viewer
};

enum class RetroPixelsPalette {
    PALETTE,      // PALette
    COLODORE,     // Colodore
    PEPTO,        // Pepto
    DEEKAY        // Deekay
};

enum class RetroPixelsDither {
    NONE,
    BAYER2X2,
    BAYER4X4,
    BAYER8X8
};

enum class RetroPixelsScale {
    NONE,
    FILL,
    FIT
};

/********************************************************
 * RetroPixels Configuration
 ********************************************************/
struct RetroPixelsConfig {
    RetroPixelsFormat format = RetroPixelsFormat::KOALA;
    RetroPixelsPalette palette = RetroPixelsPalette::COLODORE;
    RetroPixelsDither dither = RetroPixelsDither::BAYER4X4;
    RetroPixelsScale scale = RetroPixelsScale::FILL;
    bool outputPrg = false;  // If true, wrap output in PRG with viewer
    std::string viewerPath = SYSTEM_DIR "/loader/retropixels/";  // Path to viewer binaries
};

/********************************************************
 * MStream Wrapper
 ********************************************************/
class RetroPixelsMStream: public MStream 
{
private:
    std::shared_ptr<MStream> _source_stream;
    RetroPixelsConfig _config;
    std::vector<uint8_t> _output_buffer;
    bool _converted = false;
    
    bool convertImage();
    std::string getFormatExtension();

public:
    RetroPixelsMStream(std::shared_ptr<MStream> source, RetroPixelsConfig config = RetroPixelsConfig());
    ~RetroPixelsMStream();

    // MStream methods
    bool isOpen() override;
    bool open(std::ios_base::openmode mode) override;
    void close() override;
    
    uint32_t size() override;
    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;
    bool seek(uint32_t pos) override;
    
    // Configuration
    void setConfig(const RetroPixelsConfig& config) { _config = config; _converted = false; }
    RetroPixelsConfig getConfig() const { return _config; }
};

/********************************************************
 * MFile Wrapper
 ********************************************************/
class RetroPixelsMFile: public MFile
{
private:
    RetroPixelsConfig _config;
    
public:
    RetroPixelsMFile(std::string path, RetroPixelsConfig config = RetroPixelsConfig());
    
    bool isDirectory() override { return false; }
    
    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override;
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override { return is; }
    
    // Configuration
    void setConfig(const RetroPixelsConfig& config) { _config = config; }
    RetroPixelsConfig getConfig() const { return _config; }
};

/********************************************************
 * MFileSystem
 ********************************************************/
class RetroPixelsMFileSystem: public MFileSystem
{
public:
    RetroPixelsMFileSystem(): MFileSystem("retropixels") {};

    bool handles(std::string name) {
        return mstr::startsWith(name, (char *)"retropixels:", false);
    }

    MFile* getFile(std::string path) override {
        return new RetroPixelsMFile(path);
    }
};

#endif // MEATLOAF_CODEC_RETROPIXELS
