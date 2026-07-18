# TAPClean tape analysis engine (TAPClean 0.39, based on Final TAP 2.76 by
# Stewart Wilson/Subchrist Software and the TC Team, GPL v2+), vendored as an
# in-memory library: whole-image scan, ~90 loader scanners, block + PRG
# databases. Public API in include/tapclean_api.h; TAPCLEAN_EMBEDDED strips
# the CLI/report/clean code and heap-allocates the big work buffers.
idf_component_register(INCLUDE_DIRS "include"
    SRC_DIRS "." "src" "src/scanners")

target_compile_definitions(${COMPONENT_LIB} PRIVATE TAPCLEAN_EMBEDDED)

# NOTE: the engine is compiled -Os via '#pragma GCC optimize' in mydefs.h -
# the PlatformIO ESP-IDF builder ignores per-component -O compile options.
target_include_directories(${COMPONENT_LIB} PRIVATE "." "src")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-but-set-variable)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-maybe-uninitialized)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-implicit-fallthrough)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-char-subscripts)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-sign-compare)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-parentheses)
