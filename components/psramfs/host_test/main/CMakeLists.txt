idf_component_register(SRCS "host_test_psramfs.c"
                       PRIV_INCLUDE_DIRS "../.." "../../psram/src"
                       REQUIRES psram unity)
