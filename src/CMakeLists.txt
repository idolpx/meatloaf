# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

#idf_component_register(SRCS ${app_sources} $ENV{IDF_PATH}/components/esp_psram)
idf_component_register(SRCS ${app_sources})
