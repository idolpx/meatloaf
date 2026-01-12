#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <map>
#include <any>
#include "cxxopts.hpp"
#include "conversion/Quantizer.h"
#include "conversion/Converter.h"
#include "conversion/OrderedDither.h"
#include "model/PixelImage.h"
#include "model/ColorMap.h"
#include "model/Palette.h"
#include "model/IImageData.h"
#include "profiles/GraphicModes.h"
#include "profiles/ColorSpaces.h"
#include "profiles/Palettes.h"
#include "io/C64Writer.h"
#include "prepost/ImageUtils.h"
#include "prepost/Image.h"

namespace fs = std::filesystem;

const std::string viewersFolder = "/target/c64/";

struct Options {
    std::string infile;
    std::string outfile;
    std::string mode = "bitmap";
    std::string format;
    std::string ditherMode = "bayer4x4";
    std::string palette = "PALette";
    std::string colorspace = "xyz";
    std::string scale = "fill";
    int ditherRadius = 32;
    int cols = 8;
    int rows = 8;
    bool hires = false;
    bool nomaps = false;
    bool overwrite = false;
};

// Parses command-line arguments using cxxopts
Options parseArguments(int argc, char* argv[]) {
    Options options;
    cxxopts::Options cli("RetroPixels Converter", "CLI tool for converting images to retro graphics modes.");

    cli.add_options()
        ("i,infile", "Input file", cxxopts::value<std::string>(options.infile))
        ("o,outfile", "Output file", cxxopts::value<std::string>(options.outfile))
        ("m,mode", "Graphic mode", cxxopts::value<std::string>(options.mode))
        ("f,format", "Output format (png, prg)", cxxopts::value<std::string>(options.format))
        ("d,ditherMode", "Dithering mode", cxxopts::value<std::string>(options.ditherMode))
        ("r,ditherRadius", "Dither radius (0-64)", cxxopts::value<int>(options.ditherRadius))
        ("p,palette", "Color palette", cxxopts::value<std::string>(options.palette))
        ("c,colorspace", "Color space", cxxopts::value<std::string>(options.colorspace))
        ("cols", "Columns of sprites", cxxopts::value<int>(options.cols))
        ("rows", "Rows of sprites", cxxopts::value<int>(options.rows))
        ("h,hires", "Hires mode", cxxopts::value<bool>(options.hires))
        ("nomaps", "Use one color per attribute instead of a map", cxxopts::value<bool>(options.nomaps))
        ("s,scale", "Scale mode", cxxopts::value<std::string>(options.scale))
        ("overwrite", "Force overwrite of output file", cxxopts::value<bool>(options.overwrite))
        ("help", "Print usage");

    auto result = cli.parse(argc, argv);

    if (result.count("help") || options.infile.empty()) {
        std::cout << cli.help() << std::endl;
        exit(0);
    }

    return options;
}

// Check if we can overwrite a file
void checkOverwrite(const std::string& filename, bool overwrite) {
    if (fs::exists(filename) && !overwrite) {
        throw std::runtime_error("Output file " + filename + " already exists. Use --overwrite to force.");
    }
}

// Get the output filename based on the input file and format
std::string getOutFile(const std::string& infile, const std::string& extension, const std::string& specifiedOutfile) {
    if (!specifiedOutfile.empty()) {
        return specifiedOutfile;
    }
    return fs::path(infile).stem().string() + "." + extension;
}

// Save PRG executable
void saveExecutable(PixelImage& pixelImage, const std::string& outFile, const std::string& viewersFolder) {
    auto binary = toBinary(pixelImage);
    std::string viewerFile = viewersFolder + binary->getFormatName() + ".prg";

    // Try local development path first, then installed path
    std::ifstream viewer(viewerFile, std::ios::binary);
    if (!viewer) {
        // Try installed location
        std::string installedPath = "/usr/local/share/retropixels/target/c64/" + binary->getFormatName() + ".prg";
        viewer.open(installedPath, std::ios::binary);
        if (!viewer) {
            throw std::runtime_error("Executable format is not supported for " + binary->getFormatName());
        }
    }

    std::vector<uint8_t> viewerCode((std::istreambuf_iterator<char>(viewer)), std::istreambuf_iterator<char>());
    std::vector<uint8_t> buffer = toBuffer(*binary);
    
    std::ofstream out(outFile, std::ios::binary);
    out.write(reinterpret_cast<const char*>(viewerCode.data()), viewerCode.size());
    out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
}

// Main function
int main(int argc, char* argv[]) {
    try {
        Options options = parseArguments(argc, argv);

        if (!fs::exists(options.infile)) {
            throw std::runtime_error("Input file does not exist: " + options.infile);
        }

        // Get graphic mode builder function
        std::shared_ptr<PixelImage> pixelImage;
        std::map<std::string, std::any> props = {
            {"rows", options.rows},
            {"columns", options.cols},
            {"hires", options.hires},
            {"nomaps", options.nomaps}
        };

        if (options.mode == "bitmap") {
            pixelImage = GraphicModes::bitmap(props);
        } else if (options.mode == "fli") {
            pixelImage = GraphicModes::fli(props);
        } else if (options.mode == "sprites") {
            pixelImage = GraphicModes::sprites(props);
        } else {
            throw std::runtime_error("Unknown graphicMode: " + options.mode);
        }

        // Get dither preset
        auto ditherPresetIt = OrderedDither::presets.find(options.ditherMode);
        if (ditherPresetIt == OrderedDither::presets.end()) {
            throw std::runtime_error("Unknown ditherMode: " + options.ditherMode);
        }
        OrderedDither ditherer(ditherPresetIt->second, options.ditherRadius);

        // Get palette - use extern declarations
        extern Palette PALette, colodore, pepto, deekay;
        Palette* palette = &PALette; // default
        
        if (options.palette == "colodore") {
            palette = &colodore;
        } else if (options.palette == "pepto") {
            palette = &pepto;
        } else if (options.palette == "deekay") {
            palette = &deekay;
        } else if (options.palette == "PALette") {
            palette = &PALette;
        } else {
            throw std::runtime_error("Unknown palette: " + options.palette);
        }

        // Get colorspace
        auto colorspaceIt = ColorSpace::ColorSpaces.find(options.colorspace);
        if (colorspaceIt == ColorSpace::ColorSpaces.end()) {
            throw std::runtime_error("Unknown colorspace: " + options.colorspace);
        }
        
        // Wrap the colorspace function to convert from int to double
        auto colorspaceFunc = colorspaceIt->second;
        std::function<std::vector<double>(const std::vector<int>&)> colorspaceWrapper = 
            [colorspaceFunc](const std::vector<int>& pixel) -> std::vector<double> {
                std::vector<double> pixelDouble(pixel.begin(), pixel.end());
                return colorspaceFunc(pixelDouble);
            };

        Quantizer quantizer(*palette, colorspaceWrapper);
        Converter converter(quantizer);

        // Convert scale string to enum
        retropixels::ScaleMode scaleMode = retropixels::ScaleMode::FILL;
        if (options.scale == "none") {
            scaleMode = retropixels::ScaleMode::NONE;
        } else if (options.scale == "fill") {
            scaleMode = retropixels::ScaleMode::FILL;
        } else if (options.scale == "fit") {
            scaleMode = retropixels::ScaleMode::FILL; // Use FILL for fit as well
        }

        Image jimpImage = retropixels::readImage(options.infile, pixelImage->mode, scaleMode);
        if (options.ditherMode != "none") {
            ditherer.dither(jimpImage);
        }

        converter.convert(jimpImage, *pixelImage);

        std::string outFile;
        std::string appDir = fs::path(argv[0]).parent_path().string();
        std::string viewersFolder = appDir + "/target/c64/";
        
        if (options.format.empty()) {
            auto binary = toBinary(*pixelImage);
            outFile = getOutFile(options.infile, binary->getDefaultExtension(), options.outfile);
            checkOverwrite(outFile, options.overwrite);
            std::ofstream out(outFile, std::ios::binary);
            std::vector<uint8_t> buffer = toBuffer(*binary);
            out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        } else if (options.format == "prg") {
            outFile = getOutFile(options.infile, "prg", options.outfile);
            checkOverwrite(outFile, options.overwrite);
            saveExecutable(*pixelImage, outFile, viewersFolder);
        } else if (options.format == "png") {
            outFile = getOutFile(options.infile, "png", options.outfile);
            checkOverwrite(outFile, options.overwrite);
            retropixels::writeImage(*pixelImage, outFile, *palette);
        }

        std::cout << outFile << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "\nERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
