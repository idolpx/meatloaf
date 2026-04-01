# ESP-IDF component for libiscsi

set(LIBISCSI_SOURCES
    esp32_compat.c
    lib/init.c
    lib/connect.c
    lib/login.c
    lib/sync.c
    lib/pdu.c
    lib/socket.c
    lib/iscsi-command.c
    lib/scsi-lowlevel.c
    lib/discovery.c
    lib/nop.c
    lib/task_mgmt.c
    lib/md5.c
    lib/sha1.c
    lib/sha224-256.c
    lib/sha3.c
    lib/crc32c.c
    lib/logging.c
    lib/multithreading.c
    lib/utils.c
    # lib/iser.c is intentionally excluded: iSER requires Linux RDMA
)

# Register ESP-IDF component
idf_component_register(
    SRCS ${LIBISCSI_SOURCES}
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "include"
    REQUIRES lwip freertos
)

# Inject the ESP32 config.h and suppress autoconf-style macros
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    HAVE_CONFIG_H
)

# Pull in config.h before all library headers so HAVE_* guards resolve correctly
target_compile_options(${COMPONENT_LIB} PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/config.h
)

# Suppress warnings that are harmless in this context
target_compile_options(${COMPONENT_LIB} BEFORE PRIVATE
    -Wno-error=all
    -Wno-error=incompatible-pointer-types
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-format
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-sign-compare
    -Wno-implicit-fallthrough
    -Wno-type-limits
    -Wno-maybe-uninitialized
    -Wno-implicit-function-declaration
    -Wno-char-subscripts
    -Wno-unused-parameter
    -Wno-incompatible-pointer-types
    -Wno-missing-prototypes
    -Wno-strict-prototypes
    -Wno-old-style-definition
    -Wno-pedantic
)
