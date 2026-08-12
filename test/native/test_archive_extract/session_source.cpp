// meat_session.cpp on its own, kept out of engine_sources.cpp's unity build:
// it and media/archive/archive.cpp each define a file-static psram_malloc(),
// so concatenating them is a redefinition error.
#include "../../../lib/meatloaf/meat_session.cpp"
