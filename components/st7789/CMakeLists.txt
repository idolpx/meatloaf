cmake_minimum_required(VERSION 3.12)

file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
file(GLOB CSources ${CMAKE_CURRENT_SOURCE_DIR}/decoders/*.c)
set(SOURCES ${SOURCES} ${CSources})

idf_component_register( 
                        INCLUDE_DIRS "." "decoders"
                        SRCS ${SOURCES}
                        PRIV_REQUIRES driver
                      )