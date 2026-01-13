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
#include "../../include/global_defines.h"

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

// Include RetroPixels headers
// Note: STB_IMAGE_IMPLEMENTATION is already defined in components/retropixels/src/prepost/ImageUtils.cpp
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
#include "profiles/ColorSpaces.h"

// External palette declarations
extern Palette colodore;
extern Palette pepto;
extern Palette deekay;
extern Palette PALette;

/********************************************************
 * Helper Functions
 ********************************************************/

static const Palette& getPalette(RetroPixelsPalette palette) {
    switch (palette) {
        case RetroPixelsPalette::PALETTE:
            return PALette;
        case RetroPixelsPalette::COLODORE:
            return colodore;
        case RetroPixelsPalette::PEPTO:
            return pepto;
        case RetroPixelsPalette::DEEKAY:
            return deekay;
        default:
            return colodore;
    }
}

static std::shared_ptr<PixelImage> getPixelImage(RetroPixelsFormat format) {
    std::map<std::string, std::any> props;
    
    switch (format) {
        case RetroPixelsFormat::KOALA:
            props["hires"] = false;
            props["nomaps"] = false;
            return GraphicModes::bitmap(props);
            
        case RetroPixelsFormat::ARTSTUDIO:
            props["hires"] = true;
            props["nomaps"] = false;
            return GraphicModes::bitmap(props);
            
        case RetroPixelsFormat::FLI:
            props["hires"] = false;
            return GraphicModes::fli(props);
            
        case RetroPixelsFormat::AFLI:
            props["hires"] = true;
            return GraphicModes::fli(props);
            
        case RetroPixelsFormat::SPRITEPAD:
            props["rows"] = 1;
            props["columns"] = 1;
            props["hires"] = false;
            props["nomaps"] = false;
            return GraphicModes::sprites(props);
            
        default:
            props["hires"] = false;
            props["nomaps"] = false;
            return GraphicModes::bitmap(props);
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
                std::string key = kv[0];
                std::string value = kv[1];
                mstr::toLower(key);
                mstr::toLower(value);
                
                if (key == "format") {
                    if (value == "koala") config.format = RetroPixelsFormat::KOALA;
                    else if (value == "artstudio" || value == "art") config.format = RetroPixelsFormat::ARTSTUDIO;
                    else if (value == "fli") config.format = RetroPixelsFormat::FLI;
                    else if (value == "afli") config.format = RetroPixelsFormat::AFLI;
                    else if (value == "spritepad" || value == "spd") config.format = RetroPixelsFormat::SPRITEPAD;
                    else if (value == "prg") {
                        config.format = RetroPixelsFormat::KOALA;  // Default to koala for PRG
                        config.outputPrg = true;
                    }
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
                else if (key == "baseformat") {
                    // When outputPrg=true, this specifies the underlying format
                    if (value == "koala") config.format = RetroPixelsFormat::KOALA;
                    else if (value == "artstudio" || value == "art") config.format = RetroPixelsFormat::ARTSTUDIO;
                    else if (value == "fli") config.format = RetroPixelsFormat::FLI;
                    else if (value == "afli") config.format = RetroPixelsFormat::AFLI;
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
    _size = size();
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
    // If outputting PRG, always return .prg
    if (_config.outputPrg) {
        return ".prg";
    }
    
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
        case RetroPixelsFormat::PRG:
            return ".prg";
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
    
    // Check available memory before attempting conversion
#ifdef ESP_PLATFORM
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t min_required = 200000; // Require at least 200KB free
    Debug_printv("Free heap: %d bytes, required: %d bytes", free_heap, min_required);
    if (free_heap < min_required) {
        Debug_printv("Insufficient memory for image conversion");
        _error = 1;
        return false;
    }
#endif
    
    // Read source image data
    uint32_t source_size = _source_stream->size();
    if (source_size == 0) {
        Debug_printv("Source stream is empty");
        _error = 1;
        return false;
    }
    {
        std::vector<uint8_t> source_data(source_size);
        
        _source_stream->seek(0);
        uint32_t bytes_read = _source_stream->read(source_data.data(), source_size);
        
        if (bytes_read != source_size) {
            Debug_printv("Failed to read source data: read %d of %d bytes", bytes_read, source_size);
            _error = 1;
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
            _error = 1;
            return false;
        }
        
        Debug_printv("Loaded image: %dx%d, channels=%d", width, height, channels);
        Debug_printv("Free heap after image load: %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));
        
        // Create Image from loaded data
        Image image(width, height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int index = (y * width + x) * 4;
                image.setPixel(x, y, {img_data[index], img_data[index + 1], img_data[index + 2], img_data[index + 3]});
            }
        }
        stbi_image_free(img_data);
        
        Debug_printv("Free heap after Image creation: %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));
        
        // Get pixel image and palette
        Debug_printv("Creating pixel image for format %d", (int)_config.format);
        auto pixelImagePtr = getPixelImage(_config.format);
        if (!pixelImagePtr) {
            Debug_printv("Failed to create pixel image");
            _error = 1;
            return false;
        }
        Debug_printv("Free heap after getPixelImage: %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));
        const Palette& palette = getPalette(_config.palette);
        
        // Apply scaling based on config
        retropixels::ScaleMode scaleMode = retropixels::ScaleMode::FILL;
        if (_config.scale == RetroPixelsScale::NONE) {
            scaleMode = retropixels::ScaleMode::NONE;
            retropixels::crop(image, pixelImagePtr->mode);
        } else if (_config.scale == RetroPixelsScale::FILL) {
            scaleMode = retropixels::ScaleMode::FILL;
            retropixels::cropFill(image, pixelImagePtr->mode);
        } else if (_config.scale == RetroPixelsScale::FIT) {
            scaleMode = retropixels::ScaleMode::FILL; // Use FILL for fit as well
            retropixels::cropFill(image, pixelImagePtr->mode);
        }
        
        Debug_printv("Image scaled to %dx%d", image.width, image.height);
        
        // Create ordered dither using presets and apply if needed
        if (_config.dither != RetroPixelsDither::NONE) {
            std::string ditherPreset;
            int ditherRadius = 8; // default depth
            switch (_config.dither) {
                case RetroPixelsDither::BAYER2X2:
                    ditherPreset = "bayer2x2";
                    ditherRadius = 2;
                    break;
                case RetroPixelsDither::BAYER4X4:
                    ditherPreset = "bayer4x4";
                    ditherRadius = 4;
                    break;
                case RetroPixelsDither::BAYER8X8:
                    ditherPreset = "bayer8x8";
                    ditherRadius = 8;
                    break;
                default:
                    ditherPreset = "none";
                    break;
            }
            
            Debug_printv("Applying dither: %s", ditherPreset.c_str());
            auto ditherIt = OrderedDither::presets.find(ditherPreset);
            if (ditherIt != OrderedDither::presets.end()) {
                OrderedDither ditherer(ditherIt->second, ditherRadius);
                ditherer.dither(image);
            }
        }
        
        // Create colorspace function - default to oklab
        auto colorspaceIt = ColorSpace::ColorSpaces.find("oklab");
        if (colorspaceIt == ColorSpace::ColorSpaces.end()) {
            Debug_printv("oklab colorspace not found, using rgb");
            colorspaceIt = ColorSpace::ColorSpaces.find("rgb");
        }
        
        Debug_printv("Using colorspace %s", colorspaceIt->first.c_str());
        auto colorspaceFunc = colorspaceIt->second;
        std::function<std::vector<double>(const std::vector<int>&)> colorspaceWrapper = 
            [colorspaceFunc](const std::vector<int>& pixel) -> std::vector<double> {
                std::vector<double> pixelDouble(pixel.begin(), pixel.end());
                return colorspaceFunc(pixelDouble);
            };
        
        // Create quantizer and converter
        Debug_printv("Creating quantizer and converter...");
        Quantizer quantizer(palette, colorspaceWrapper);
        Converter converter(quantizer);
        
        // Convert the image
        Debug_printv("Converting image...");
        converter.convert(image, *pixelImagePtr);
        
        // Convert to binary format
        Debug_printv("Converting to binary format...");
        auto binaryFormat = toBinary(*pixelImagePtr);
        if (!binaryFormat) {
            Debug_printv("Failed to create binary format");
            _error = 1;
            return false;
        }
        
        // Convert to buffer
        _output_buffer = toBuffer(*binaryFormat);
        
        // Wrap in PRG with viewer if requested
        if (_config.outputPrg) {
            std::string formatName = binaryFormat->getFormatName();
            std::string viewerFile = "/sd" + _config.viewerPath + formatName + ".prg";
            
            // Try to open viewer file using MFile from SD card
            auto viewerMFile = MFSOwner::File(viewerFile);
            if (!viewerMFile) {
                // Try to open viewer file using MFile from flash
                viewerFile = _config.viewerPath + formatName + ".prg";
                viewerMFile = MFSOwner::File(viewerFile);
            }

            Debug_printv("Wrapping in PRG with viewer: %s", viewerFile.c_str());

            if (viewerMFile && viewerMFile->exists()) {
                auto viewerStream = viewerMFile->getSourceStream(std::ios_base::in);
                if (viewerStream && viewerStream->open(std::ios_base::in)) {
                    uint32_t viewerSize = viewerStream->size();
                    
                    // Read viewer into temporary buffer
                    std::vector<uint8_t> viewerCode(viewerSize);
                    uint32_t bytesRead = viewerStream->read(viewerCode.data(), viewerSize);
                    viewerStream->close();
                    
                    if (bytesRead == viewerSize) {
                        // Prepend viewer code to output buffer
                        std::vector<uint8_t> prgBuffer;
                        prgBuffer.reserve(viewerSize + _output_buffer.size());
                        prgBuffer.insert(prgBuffer.end(), viewerCode.begin(), viewerCode.end());
                        prgBuffer.insert(prgBuffer.end(), _output_buffer.begin(), _output_buffer.end());
                        _output_buffer = std::move(prgBuffer);
                        
                        Debug_printv("PRG created: viewer=%d bytes, data=%d bytes, total=%d bytes", 
                                    viewerSize, _output_buffer.size() - viewerSize, _output_buffer.size());
                    } else {
                        Debug_printv("Failed to read viewer file completely: read %d of %d bytes", bytesRead, viewerSize);
                    }
                } else {
                    Debug_printv("Failed to open viewer stream");
                }
            } else {
                Debug_printv("Viewer file not found: %s (PRG wrapping disabled)", viewerFile.c_str());
            }
        }
        
        _size = _output_buffer.size();
        _position = 0;
        _converted = true;
        
        Debug_printv("Conversion successful: %d bytes", _size);
        return true;
    }  // End of scope block
}
uint32_t RetroPixelsMStream::size()
{
    static uint32_t base_size = 0;
    if (base_size != 0) {
        return base_size;
    }

    // If we've already converted, return the actual size
    if (_converted) {
        return _size;
    }
    
    // Return known fixed sizes for C64 formats to avoid expensive conversion
    // These are the standard sizes for each format
    switch (_config.format) {
        case RetroPixelsFormat::KOALA:
            base_size = 10003;  // 2 load addr + 8000 bitmap + 1000 screen + 1000 color + 1 bg
            break;
        case RetroPixelsFormat::ARTSTUDIO:
            base_size = 9009;   // 2 load addr + 8000 bitmap + 1000 screen + 7 (border + unused)
            break;
        case RetroPixelsFormat::FLI:
            base_size = 17474;  // FLI format size
            break;
        case RetroPixelsFormat::AFLI:
            base_size = 19178;  // AFLI format size
            break;
        case RetroPixelsFormat::SPRITEPAD:
            base_size = 200;    // Approximate size for single sprite
            break;
        default:
            base_size = 10003;  // Default to Koala
            break;
    }
    
    // If outputting PRG, add viewer size
    if (_config.outputPrg) {
        // Determine format name for viewer
        std::string formatName;
        switch (_config.format) {
            case RetroPixelsFormat::KOALA:
                formatName = "koala";
                break;
            case RetroPixelsFormat::ARTSTUDIO:
                formatName = "artstudio";
                break;
            case RetroPixelsFormat::FLI:
                formatName = "fli";
                break;
            case RetroPixelsFormat::AFLI:
                formatName = "afli";
                break;
            case RetroPixelsFormat::SPRITEPAD:
                formatName = "spritepad";
                break;
            default:
                formatName = "koala";
                break;
        }
        
        // Try to get viewer file size
        std::string viewerFile = "/sd" + _config.viewerPath + formatName + ".prg";
        auto viewerMFile = MFSOwner::File(viewerFile);
        if (!viewerMFile || !viewerMFile->exists()) {
            // Try flash location
            viewerFile = _config.viewerPath + formatName + ".prg";
            viewerMFile = MFSOwner::File(viewerFile);
        }
        
        if (viewerMFile && viewerMFile->exists()) {
            auto viewerStream = viewerMFile->getSourceStream(std::ios_base::in);
            if (viewerStream) {
                uint32_t viewer_size = viewerStream->size();
                //Debug_printv("Viewer size for %s: %d bytes", formatName.c_str(), viewer_size);
                return base_size + viewer_size;
            }
        }
        
        // If viewer not found, just return base size
        Debug_printv("Viewer file not found: %s", viewerFile.c_str());
    }
    
    return base_size;
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
    mstr::trim(url_str);
    
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
    // Parse URL and extract configuration
    pathInStream = parseRetroPixelsUrl(path, _config);
    parseURL(pathInStream);
    Debug_printv("path[%s] pathInStream[%s]", path.c_str(), pathInStream.c_str());
}

std::shared_ptr<MStream> RetroPixelsMFile::getSourceStream(std::ios_base::openmode mode)
{
    // Get the source file's stream
    Debug_printv("Opening source file: url[%s] pathInStream[%s]", url.c_str(), pathInStream.c_str());
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
    parseURL(pathInStream);
    return retroStream;
}
