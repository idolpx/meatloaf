file(GLOB_RECURSE SRCS "lib/*.c")

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS . include)