idf_component_register(SRCS test_psramfs.c
                       PRIV_INCLUDE_DIRS .
                       PRIV_REQUIRES psram unity vfs
                       WHOLE_ARCHIVE
                      )
