# RetroPixels C++ Implementation

A C++ port of the RetroPixels image converter for Commodore 64 graphics formats.

## Overview

This is a feature-complete C++ implementation of RetroPixels that converts modern images to C64 graphics formats including Koala Painter, Art Studio, FLI, and sprite formats. It provides the same functionality as the Node.js version with native performance.

## Features

- **Image Formats**: Koala (.kla), Art Studio (.art), FLI (.fli), Sprites (.spd)
- **Output Modes**: Native binary formats, PNG preview, PRG executables
- **Color Palettes**: PALette (default), colodore, pepto, deekay
- **Color Spaces**: XYZ (default), YUV, RGB, okLab
- **Dithering**: Bayer2x2, Bayer4x4 (default), Bayer8x8, none
- **Scaling**: Fill (default), fit, none
- **Graphic Modes**: Bitmap (multicolor/hires), FLI (multicolor/hires), Sprites

## Building

### Requirements

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- Make

### Compile

```bash
make
```

This will:
- Compile all source files in parallel
- Create the `bin/retropixels` executable
- Copy viewer PRG files to `bin/target/c64/`

### Other Make Targets

```bash
make clean      # Remove all build artifacts
make install    # Install to /usr/local/bin (requires sudo)
make test       # Run basic tests
```

## Usage

### Basic Conversion

```bash
./bin/retropixels -i input.jpg -o output.kla
```

### Common Options

```bash
# Hires mode
./bin/retropixels -i input.jpg -o output.kla -h

# Different palette
./bin/retropixels -i input.jpg -o output.kla -p pepto

# Different dithering
./bin/retropixels -i input.jpg -o output.kla -d bayer8x8

# No dithering
./bin/retropixels -i input.jpg -o output.kla -d none

# PNG output (preview)
./bin/retropixels -i input.jpg -o output.png -f png

# PRG executable (for C64 emulator/hardware)
./bin/retropixels -i input.jpg -o output.prg -f prg

# FLI mode
./bin/retropixels -i input.jpg -o output.fli -m fli
```

### Full Command-Line Options

```
Options:
  -i, --infile <file>           Input image file (required)
  -o, --outfile <file>          Output file (optional, uses input name with appropriate extension)
  -m, --mode <mode>             Graphic mode: bitmap (default), fli, sprites
  -f, --format <format>         Output format: png, prg (default: native format)
  -d, --ditherMode <mode>       Dithering: bayer2x2, bayer4x4 (default), bayer8x8, none
  -r, --ditherRadius <0-64>     Dither radius (default: 32)
  -p, --palette <name>          Color palette: PALette (default), colodore, pepto, deekay
  -c, --colorspace <space>      Color space: xyz (default), yuv, rgb, oklab
  -h, --hires                   Enable hires mode (2x horizontal resolution, 2 colors per cell)
  --nomaps                      Use one color per attribute instead of a map
  -s, --scale <mode>            Scale mode: fill (default), fit, none
  --cols <n>                    Columns of sprites (default: 8)
  --rows <n>                    Rows of sprites (default: 8)
  --overwrite                   Force overwrite of output file
  --help                        Display help message
```

## Implementation Details

### Architecture

The codebase is organized into several modules:

- **conversion/**: Quantization, color conversion, ordered dithering
- **io/**: Binary format readers/writers (Koala, Art Studio, FLI, Sprites)
- **model/**: Core data structures (PixelImage, ColorMap, Palette, GraphicMode)
- **prepost/**: Image loading, scaling, preprocessing using STB libraries
- **profiles/**: Predefined palettes, color spaces, and graphic mode configurations

### Image Processing

The implementation uses:
- **STB libraries**: For image loading (stb_image) and saving (stb_image_write)
- **Jimp-compatible bicubic interpolation**: Custom implementation matching Node.js Jimp's resize algorithm
- **Two-pass resize**: Horizontal interpolation followed by vertical interpolation for quality

### Key Algorithms

1. **Image Scaling**: Jimp-compatible bicubic interpolation with edge extrapolation
2. **Color Quantization**: Euclidean distance in selected color space
3. **Ordered Dithering**: Bayer matrix dithering with configurable radius
4. **Color Mapping**: Multi-map conversion to match C64 color constraints

## Comparison with Node.js Version

### File Size Matching

The C++ implementation produces files with identical sizes to the Node.js version:

- **Koala (multicolor)**: 10,003 bytes (.kla)
- **Koala (hires)**: 9,009 bytes (.art)
- **FLI (multicolor)**: 17,219 bytes (.fli)
- **FLI (hires)**: 16,194 bytes (.fli)
- **Koala PRG**: 16,146 bytes (.prg)
- **Hires PRG**: 15,152 bytes (.prg)
- **FLI PRG**: 30,530 bytes (.prg)

### Feature Parity

✅ All major features implemented
✅ Same command-line interface
✅ Same graphic modes (bitmap, FLI, sprites)
✅ Same output formats (native, PNG, PRG)
✅ Same palettes and color spaces
✅ Same dithering algorithms

### Known Differences

- **Pixel-level differences**: Due to floating-point rounding differences between JavaScript and C++, the exact pixel values may differ slightly from the Node.js version, though the visual output is equivalent
- **AFLI mode**: Not implemented in either version (despite being listed in help text)

## Dependencies

The project uses header-only libraries included in the source:

- **cxxopts**: Command-line argument parsing
- **stb_image.h**: Image loading (JPEG, PNG, BMP, etc.)
- **stb_image_write.h**: Image writing (PNG, JPEG)
- **stb_image_resize2.h**: Image resizing (not used, custom implementation preferred)

## Performance

The C++ implementation is significantly faster than the Node.js version due to:
- Native compilation
- Efficient memory management
- Parallel compilation support in build system

## License

MIT License. Same as the original RetroPixels project.

## Credits

- Original RetroPixels: Michel de Bree
- C++ Implementation: James Johnston
- STB Libraries: Sean Barrett (public domain)
- cxxopts: Jarryd Beck (MIT License)
