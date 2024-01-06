SRC = ../src

SOURCE_FILES = $(SRC)/psramfs_cache.c \
	$(SRC)/psramfs_check.c \
	$(SRC)/psramfs_gc.c \
	$(SRC)/psramfs_hydrogen.c \
	$(SRC)/psramfs_nucleus.c \
	python_ops.c

INCLUDES = -I . \
	-I $(SRC)/ \
	-I $(SRC)/default

psramfs_.so: $(SOURCE_FILES)
	$(CC) -g3 -fPIC -shared $(INCLUDES) -o $@ $(SOURCE_FILES) -lm

clean:
	rm -rf psramfs_.so *~ *.pyc
