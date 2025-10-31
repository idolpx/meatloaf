cmake_minimum_required(VERSION 3.12)

# Collect all C sources (recursively) so subdirectories like external/ are
# included. Using a non-recursive glob omitted many .c files and caused
# undefined symbols at link time.
file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

idf_component_register(
    INCLUDE_DIRS include src
    SRCS ${SOURCES}
    PRIV_REQUIRES esp_netif mbedtls
)

# Prefer using a compile definition instead of raw -D flags.
target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CONFIG_H)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MBEDTLS_GCM_ALT)
