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

#include "retropixels.h"

// Include MFileSystem owner for creating files from URLs
#include "meatloaf.h"
#include "string_utils.h"

// Include RetroPixels headers
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_resize2.h"

#include "model/IImageData.h"
#include "model/PixelImage.h"
#include "model/GraphicMode.h"
#include "model/Palette.h"
#include "conversion/Converter.h"
#include "conversion/Quantizer.h"
#include "conversion/OrderedDither.h"
#include "prepost/ImageUtils.h"
#include "io/C64Writer.h"
#include "profiles/Palettes.h"
#include "profiles/GraphicModes.h"

/********************************************************
 * Helper Functions
 ********************************************************/

static const Palette& getPalette(RetroPixelsPalette palette) {
    switch (palette) {
        case RetroPixelsPalette::PALETTE:
            return retropixels::Palettes::palette();
        case RetroPixelsPalette::COLODORE:
            return retropixels::Palettes::colodore();
        case RetroPixelsPalette::PEPTO:
            return retropixels::Palettes::pepto();
        case RetroPixelsPalette::DEEKAY:
            return retropixels::Palettes::deekay();
        default:
            return retropixels::Palettes::colodore();
    }
}

static const GraphicMode& getGraphicMode(RetroPixelsFormat format) {
    switch (format) {
        case RetroPixelsFormat::KOALA:
            return retropixels::GraphicModes::koala();
        case RetroPixelsFormat::ARTSTUDIO:
            return retropixels::GraphicModes::artStudio();
        case RetroPixelsFormat::FLI:
            return retropixels::GraphicModes::fli();
        case RetroPixelsFormat::AFLI:
            return retropixels::GraphicModes::afli();
        case RetroPixelsFormat::SPRITEPAD:
            return retropixels::GraphicModes::spritePad();
        default:
            return retropixels::GraphicModes::koala();
    }
}

static retropixels::ScaleMode getScaleMode(RetroPixelsScale scale) {
    switch (scale) {
        case RetroPixelsScale::NONE:
            return retropixels::ScaleMode::NONE;
        case RetroPixelsScale::FILL:
            return retropixels::ScaleMode::FILL;
        case RetroPixelsScale::FIT:
            // FIT not directly supported, use FILL as fallback
            return retropixels::ScaleMode::FILL;
        default:
            return retropixels::ScaleMode::FILL;
    }
}

/**
 * Parse retropixels: URL and extract source URL and configuration
 * Format: retropixels:<source_url>#format=koala&palette=colodore&dither=bayer4x4&scale=fill
 * Returns: source_url (without retropixels: prefix and fragment)
 * Modifies: config parameter with parsed values
 */
static std::string parseRetroPixelsUrl(const std::string& url_str, RetroPixelsConfig& config) {
    std::string path = url_str;
    
    // Remove "retropixels:" prefix if present
    if (mstr::startsWith(path, "retropixels:")) {
        path = path.substr(12);  // strlen("retropixels:")
    }
    
    // Parse configuration from URL fragment if present
    size_t fragment_pos = path.find('#');
    if (fragment_pos != std::string::npos) {
        std::string source_url = path.substr(0, fragment_pos);
        std::string fragment = path.substr(fragment_pos + 1);
        
        // Parse fragment parameters
        std::vector<std::string> params = mstr::split(fragment, '&');
        for (const auto& param : params) {
            std::vector<std::string> kv = mstr::split(param, '=');
            if (kv.size() == 2) {
                std::string key = mstr::toLower(kv[0]);
                std::string value = mstr::toLower(kv[1]);
                
                if (key == "format") {
                    if (value == "koala") config.format = RetroPixelsFormat::KOALA;
                    else if (value == "artstudio" || value == "art") config.format = RetroPixelsFormat::ARTSTUDIO;
                    else if (value == "fli") config.format = RetroPixelsFormat::FLI;
                    else if (value == "afli") config.format = RetroPixelsFormat::AFLI;
                    else if (value == "spritepad" || value == "spd") config.format = RetroPixelsFormat::SPRITEPAD;
                }
                else if (key == "palette") {
                    if (value == "palette") config.palette = RetroPixelsPalette::PALETTE;
                    else if (value == "colodore") config.palette = RetroPixelsPalette::COLODORE;
                    else if (value == "pepto") config.palette = RetroPixelsPalette::PEPTO;
                    else if (value == "deekay") config.palette = RetroPixelsPalette::DEEKAY;
                }
                else if (key == "dither") {
                    if (value == "none") config.dither = RetroPixelsDither::NONE;
                    else if (value == "bayer2x2") config.dither = RetroPixelsDither::BAYER2X2;
                    else if (value == "bayer4x4") config.dither = RetroPixelsDither::BAYER4X4;
                    else if (value == "bayer8x8") config.dither = RetroPixelsDither::BAYER8X8;
                }
                else if (key == "scale") {
                    if (value == "none") config.scale = RetroPixelsScale::NONE;
                    else if (value == "fill") config.scale = RetroPixelsScale::FILL;
                    else if (value == "fit") config.scale = RetroPixelsScale::FIT;
                }
            }
        }
        
        return source_url;
    }
    
    return path;
}

/********************************************************
 * RetroPixelsMStream Implementation
 ********************************************************/

RetroPixelsMStream::RetroPixelsMStream(std::shared_ptr<MStream> source, RetroPixelsConfig config)
    : MStream(source->url), _source_stream(source), _config(config), _converted(false)
{
    Debug_printv("source[%s]", source->url.c_str());
}

RetroPixelsMStream::~RetroPixelsMStream()
{
    close();
}

bool RetroPixelsMStream::isOpen()
{
    return _source_stream && _source_stream->isOpen();
}

bool RetroPixelsMStream::open(std::ios_base::openmode mode)
{
    if (!_source_stream) {
        return false;
    }
    
    if (!_source_stream->isOpen()) {
        if (!_source_stream->open(mode)) {
            return false;
        }
    }
    
    return true;
}

void RetroPixelsMStream::close()
{
    if (_source_stream) {
        _source_stream->close();
    }
    _output_buffer.clear();
    _converted = false;
    _position = 0;
}

std::string RetroPixelsMStream::getFormatExtension()
{
    switch (_config.format) {
        case RetroPixelsFormat::KOALA:
            return ".kla";
        case RetroPixelsFormat::ARTSTUDIO:
            return ".art";
        case RetroPixelsFormat::FLI:
            return ".fli";
        case RetroPixelsFormat::AFLI:
            return ".afli";
        case RetroPixelsFormat::SPRITEPAD:
            return ".spd";
        default:
            return ".kla";
    }
}

bool RetroPixelsMStream::convertImage()
{
    if (_converted) {
        return true;
    }
    
    if (!_source_stream || !_source_stream->isOpen()) {
        Debug_printv("Source stream not open");
        return false;
    }
    
    try {
        // Read source image data
        uint32_t source_size = _source_stream->size();
        std::vector<uint8_t> source_data(source_size);
        
        _source_stream->seek(0);
        uint32_t bytes_read = _source_stream->read(source_data.data(), source_size);
        
        if (bytes_read != source_size) {
            Debug_printv("Failed to read source data: read %d of %d bytes", bytes_read, source_size);
            return false;
        }
        
        // Load image using stb_image
        int width, height, channels;
        unsigned char* img_data = stbi_load_from_memory(
            source_data.data(), 
            source_size, 
            &width, 
            &height, 
            &channels, 
            4  // Force RGBA
        );
        
        if (!img_data) {
            Debug_printv("Failed to decode image: %s", stbi_failure_reason());
            return false;
        }
        
        Debug_printv("Loaded image: %dx%d, channels=%d", width, height, channels);
        
        // Create IImageData from loaded image
        IImageData imageData(width, height);
        memcpy(imageData.data.data(), img_data, width * height * 4);
        stbi_image_free(img_data);
        
        // Get graphic mode and palette
        const GraphicMode& mode = getGraphicMode(_config.format);
        const Palette& palette = getPalette(_config.palette);
        
        // Scale image if needed
        retropixels::ScaleMode scaleMode = getScaleMode(_config.scale);
        
        // TODO: Implement proper scaling with stb_image_resize2
        // For now, we'll create the image as-is
        
        // Create ordered dither
        std::unique_ptr<OrderedDither> dither;
        switch (_config.dither) {
            case RetroPixelsDither::BAYER2X2:
                dither = std::make_unique<OrderedDither>(2);
                break;
            case RetroPixelsDither::BAYER4X4:
                dither = std::make_unique<OrderedDither>(4);
                break;
            case RetroPixelsDither::BAYER8X8:
                dither = std::make_unique<OrderedDither>(8);
                break;
            case RetroPixelsDither::NONE:
            default:
                dither = nullptr;
                break;
        }
        
        // Create quantizer and converter
        Quantizer quantizer(palette, dither.get());
        Converter converter(quantizer);
        
        // Create pixel image for conversion
        PixelImage pixelImage(mode);
        
        // Convert the image
        converter.convert(imageData, pixelImage);
        
        // Convert to binary format
        auto binaryFormat = toBinary(pixelImage);
        if (!binaryFormat) {
            Debug_printv("Failed to create binary format");
            return false;
        }
        
        // Convert to buffer
        _output_buffer = toBuffer(*binaryFormat);
        
        _size = _output_buffer.size();
        _position = 0;
        _converted = true;
        
        Debug_printv("Conversion successful: %d bytes", _size);
        return true;
        
    } catch (const std::exception& e) {
        Debug_printv("Exception during conversion: %s", e.what());
        return false;
    }
}

uint32_t RetroPixelsMStream::size()
{
    if (!_converted) {
        if (!convertImage()) {
            return 0;
        }
    }
    return _size;
}

uint32_t RetroPixelsMStream::read(uint8_t* buf, uint32_t size)
{
    if (!_converted) {
        if (!convertImage()) {
            return 0;
        }
    }
    
    if (_position >= _size) {
        return 0;
    }
    
    uint32_t available = _size - _position;
    if (size > available) {
        size = available;
    }
    
    memcpy(buf, _output_buffer.data() + _position, size);
    _position += size;
    
    return size;
}

uint32_t RetroPixelsMStream::write(const uint8_t *buf, uint32_t size)
{
    // Parse URL from buffer
    std::string url_str(reinterpret_cast<const char*>(buf), size);
    url_str = mstr::trim(url_str);
    
    Debug_printv("Converting image from URL: %s", url_str.c_str());
    
    // Parse URL and extract configuration
    std::string source_url = parseRetroPixelsUrl(url_str, _config);
    
    Debug_printv("Source URL: %s", source_url.c_str());
    
    // Create MFile for the source URL
    auto sourceFile = MFSOwner::File(source_url);
    if (!sourceFile) {
        Debug_printv("Failed to create file for URL: %s", source_url.c_str());
        return 0;
    }
    
    // Get the source stream
    auto newSourceStream = sourceFile->getSourceStream(std::ios_base::in);
    if (!newSourceStream) {
        Debug_printv("Failed to get source stream");
        return 0;
    }
    
    // Open the source stream
    if (!newSourceStream->open(std::ios_base::in)) {
        Debug_printv("Failed to open source stream");
        return 0;
    }
    
    // Replace the current source stream
    if (_source_stream) {
        _source_stream->close();
    }
    _source_stream = newSourceStream;
    
    // Reset conversion state
    _converted = false;
    _output_buffer.clear();
    _position = 0;
    _size = 0;
    
    Debug_printv("Successfully set source stream, will convert on next read");
    return size;
}

bool RetroPixelsMStream::seek(uint32_t pos)
{
    if (!_converted) {
        if (!convertImage()) {
            return false;
        }
    }
    
    if (pos > _size) {
        pos = _size;
    }
    
    _position = pos;
    return true;
}

/********************************************************
 * RetroPixelsMFile Implementation
 ********************************************************/

RetroPixelsMFile::RetroPixelsMFile(std::string path, RetroPixelsConfig config)
    : MFile(path), _config(config)
{
    Debug_printv("path[%s]", path.c_str());
    
    // Parse URL and extract configuration
    pathInStream = parseRetroPixelsUrl(path, _config);
}

std::shared_ptr<MStream> RetroPixelsMFile::getSourceStream(std::ios_base::openmode mode)
{
    // Get the source file's stream
    auto sourceFile = MFSOwner::File(pathInStream);
    if (!sourceFile) {
        Debug_printv("Failed to open source file: %s", pathInStream.c_str());
        return nullptr;
    }
    
    auto sourceStream = sourceFile->getSourceStream(mode);
    if (!sourceStream) {
        Debug_printv("Failed to get source stream");
        return nullptr;
    }
    
    // Wrap it in RetroPixelsMStream
    auto retroStream = std::make_shared<RetroPixelsMStream>(sourceStream, _config);
    size = retroStream->size();
    return retroStream;
}
