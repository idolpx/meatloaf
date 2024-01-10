idf_build_get_property(target IDF_TARGET)

list(APPEND srcs "psramfs_api.c"
                 "psramfs/src/psramfs_cache.c"
                 "psramfs/src/psramfs_check.c"
                 "psramfs/src/psramfs_gc.c"
                 "psramfs/src/psramfs_hydrogen.c"
                 "psramfs/src/psramfs_nucleus.c")

if(NOT ${target} STREQUAL "linux")
    list(APPEND pr bootloader_support esptool_py vfs)
    list(APPEND srcs "esp_psramfs.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "." "psramfs/src"
                       REQUIRES esp_partition
                       PRIV_REQUIRES ${pr} spi_flash)

if(CMAKE_C_COMPILER_ID MATCHES "GNU")
    set_source_files_properties(psramfs/src/psramfs_nucleus.c PROPERTIES COMPILE_FLAGS -Wno-stringop-truncation)
endif()
