ifndef psram
$(warn defaulting path to generic psram module, psram variable not set)
psram = ../generic/psram
endif
FLAGS	+= -DCONFIG_BUILD_SPIFFS
INC	+= -I${psram}/src
CPATH	+= ${psram}/src
CFILES	+= psramfs_nucleus.c
CFILES	+= psramfs_gc.c
CFILES	+= psramfs_hydrogen.c
CFILES	+= psramfs_cache.c
CFILES	+= psramfs_check.c
