cmake_minimum_required(VERSION 3.12)

file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

idf_component_register( 
  INCLUDE_DIRS src
  SRCS ${SOURCES}
  REQUIRES driver esp_timer esp_rom nvs_flash
)

component_compile_options(-Wno-error=format= -Wno-format)
# target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-unused-function)
# target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-implicit-int)
# target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-return-type)
# target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-format-extra-args)
# target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-unused-parameter)
# target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-unused-const-variable)
# target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-implicit-fallthrough)