# psramfs_create_partition_image
#
# Create a psram image of the specified directory on the host during build and optionally
# have the created image flashed using `idf.py flash`
function(psramfs_create_partition_image partition base_dir)
    set(options FLASH_IN_PROJECT)
    set(multi DEPENDS)
    cmake_parse_arguments(arg "${options}" "" "${multi}" "${ARGN}")

    idf_build_get_property(idf_path IDF_PATH)
    set(psramfsgen_py ${PYTHON} ${idf_path}/components/psram/psramfsgen.py)

    get_filename_component(base_dir_full_path ${base_dir} ABSOLUTE)

    partition_table_get_partition_info(size "--partition-name ${partition}" "size")
    partition_table_get_partition_info(offset "--partition-name ${partition}" "offset")

    if("${size}" AND "${offset}")
        set(image_file ${CMAKE_BINARY_DIR}/${partition}.bin)

        if(CONFIG_PSRAMFS_USE_MAGIC)
            set(use_magic "--use-magic")
        endif()

        if(CONFIG_PSRAMFS_USE_MAGIC_LENGTH)
            set(use_magic_len "--use-magic-len")
        endif()

        if(CONFIG_PSRAMFS_FOLLOW_SYMLINKS)
            set(follow_symlinks "--follow-symlinks")
        endif()

        # Execute PSRAMFS image generation; this always executes as there is no way to specify for CMake to watch for
        # contents of the base dir changing.
        add_custom_target(psramfs_${partition}_bin ALL
            COMMAND ${psramfsgen_py} ${size} ${base_dir_full_path} ${image_file}
            --page-size=${CONFIG_PSRAMFS_PAGE_SIZE}
            --obj-name-len=${CONFIG_PSRAMFS_OBJ_NAME_LEN}
            --meta-len=${CONFIG_PSRAMFS_META_LENGTH}
            ${follow_symlinks}
            ${use_magic}
            ${use_magic_len}
            DEPENDS ${arg_DEPENDS}
            )

        set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY
            ADDITIONAL_MAKE_CLEAN_FILES
            ${image_file})

        idf_component_get_property(main_args esptool_py FLASH_ARGS)
        idf_component_get_property(sub_args esptool_py FLASH_SUB_ARGS)
        # Last (optional) parameter is the encryption for the target. In our
        # case, psram is not encrypt so pass FALSE to the function.
        esptool_py_flash_target(${partition}-flash "${main_args}" "${sub_args}" ALWAYS_PLAINTEXT)
        esptool_py_flash_to_partition(${partition}-flash "${partition}" "${image_file}")

        add_dependencies(${partition}-flash psramfs_${partition}_bin)

        if(arg_FLASH_IN_PROJECT)
            esptool_py_flash_to_partition(flash "${partition}" "${image_file}")
            add_dependencies(flash psramfs_${partition}_bin)
        endif()
    else()
        set(message "Failed to create PSRAMFS image for partition '${partition}'. "
                    "Check project configuration if using the correct partition table file.")
        fail_at_build_time(psramfs_${partition}_bin "${message}")
    endif()
endfunction()
