# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE SOURCES 
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/audio/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/gpib/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/iec/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/userport/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/compat/*.c
    ${CMAKE_SOURCE_DIR}/lib/config/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/console/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/console/Commands/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/console/ute/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/console/improv/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/console/Helpers/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/iec/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/display/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/encoding/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/encrypt/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/FileSystem/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/fn_esp_http_client/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/fnjson/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/ftp/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/fuji/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/gpiox/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/hardware/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/http/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/meatloaf/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/**/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/network-protocol/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/printer-emulator/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/qrcode/*.c
    ${CMAKE_SOURCE_DIR}/lib/qrcode/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/sam/*.c
    ${CMAKE_SOURCE_DIR}/lib/server/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/ssdp/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/task/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/tcpip/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/telnet/*.c
    ${CMAKE_SOURCE_DIR}/lib/tinyxml2/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/TNFSlib/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/utils/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/vdrive/*.c
    ${CMAKE_SOURCE_DIR}/lib/vdrive/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/webdav/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/wifi/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/www/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/graphql/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/rest/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/webdav/*.cpp
)

set(INCLUDES ${CMAKE_SOURCE_DIR}/include)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/include/pinmap)

set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/audio)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/bus)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/bus/gpib)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/bus/iec)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/bus/userport)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/compat)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/config)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/console)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/console/Commands)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/console/cxxopts)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/console/Helpers)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/console/improv)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/console/ute)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/device)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/display)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/encoding)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/encrypt)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/FileSystem)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/fn_esp_http_client)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/fnjson)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/ftp)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/fuji)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/gpiox)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/hardware)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/http)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/meatloaf)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/media)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/network-protocol)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/printer-emulator)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/qrcode)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/runcpm)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/sam)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/server)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/ssdp)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/task)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/tcpip)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/telnet)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/tinyxml2)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/TNFSlib)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/utils)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/vdrive)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/webdav)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/wifi)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/www)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/www/graphql)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/www/rest)
set(INCLUDES ${INCLUDES} ${CMAKE_SOURCE_DIR}/lib/www/webdav)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${INCLUDES}
    PRIV_REQUIRES esp_driver_uart esp_netif esp_driver_gpio fatfs vfs json esp_wifi mbedtls console app_update spi_flash mlff esp_driver_ledc expat http_parser esp-tls tcp_transport esp_driver_gptimer esp_driver_tsens esp_http_client libssh
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-value)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-but-set-variable)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-return-type)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-extra-args)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-implicit-fallthrough)