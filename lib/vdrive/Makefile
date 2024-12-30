CDEFS=-g

CPPOBJECTS=main.oo VDriveClass.oo

OBJECTS=lib.o log.o util.o cbmfile.o rawfile.o charset.o cbmdos.o \
        diskcontents.o diskcontents-block.o imagecontents.o cbmimage.o \
        vdrive.o vdrive-iec.o vdrive-command.o vdrive-bam.o vdrive-dir.o vdrive-rel.o vdrive-internal.o \
        diskimage.o fsimage.o fsimage-p64.o fsimage-dxx.o fsimage-gcr.o fsimage-create.o fsimage-probe.o fsimage-check.o \
        gcr.o p64.o zfile.o archdep-win.o

vdrive.exe: $(OBJECTS) $(CPPOBJECTS)
	g++ $(OBJECTS) $(CPPOBJECTS) -o vdrive.exe

$(OBJECTS): %.o: %.c
	gcc -c $(CDEFS) $< -o $@

$(CPPOBJECTS): %.oo: %.cpp
	g++ -c $(CDEFS) $< -o $@

clean:
	rm -f $(OBJECTS) $(CPPOBJECTS) *~ vdrive.exe

deps:
	gcc -MM *.c *.cpp >> Makefile


# --------------------------------------------------------------------------------------------------
# The following list of dependencies can be created by typing "make deps"
# --------------------------------------------------------------------------------------------------

archdep-win.o: archdep-win.c archdep.h lib.h types.h log.h
cbmdos.o: cbmdos.c cbmdos.h types.h lib.h log.h
cbmfile.o: cbmfile.c archdep.h cbmdos.h types.h charset.h fileio.h lib.h \
 log.h rawfile.h cbmfile.h
cbmimage.o: cbmimage.c diskimage.h types.h archdep.h p64.h p64config.h \
 lib.h log.h cbmimage.h
charset.o: charset.c charset.h types.h lib.h log.h util.h archdep.h
diskcontents.o: diskcontents.c diskcontents-block.h diskcontents.h \
 imagecontents.h types.h lib.h log.h vdrive.h vdrive-dir.h cbmdos.h \
 vdrive-internal.h
diskcontents-block.o: diskcontents-block.c cbmdos.h types.h \
 diskcontents-block.h diskimage.h archdep.h p64.h p64config.h lib.h log.h \
 imagecontents.h vdrive-bam.h vdrive-dir.h vdrive-internal.h vdrive.h
diskimage.o: diskimage.c diskconstants.h diskimage.h types.h archdep.h \
 p64.h p64config.h lib.h log.h fsimage-check.h fsimage-create.h \
 fsimage-dxx.h fsimage-gcr.h fsimage-p64.h fsimage.h
fsimage.o: fsimage.c archdep.h diskconstants.h diskimage.h types.h p64.h \
 p64config.h lib.h log.h fsimage-dxx.h fsimage-gcr.h fsimage-p64.h \
 fsimage-probe.h fsimage.h zfile.h util.h cbmdos.h
fsimage-check.o: fsimage-check.c diskconstants.h diskimage.h types.h \
 archdep.h p64.h p64config.h lib.h log.h fsimage-check.h
fsimage-create.o: fsimage-create.c archdep.h diskconstants.h diskimage.h \
 types.h p64.h p64config.h lib.h log.h fsimage-create.h fsimage.h gcr.h \
 cbmdos.h util.h x64.h
fsimage-dxx.o: fsimage-dxx.c diskconstants.h diskimage.h types.h \
 archdep.h p64.h p64config.h lib.h log.h cbmdos.h fsimage-dxx.h fsimage.h \
 gcr.h util.h x64.h
fsimage-gcr.o: fsimage-gcr.c diskconstants.h diskimage.h types.h \
 archdep.h p64.h p64config.h lib.h log.h fsimage-gcr.h fsimage.h gcr.h \
 cbmdos.h util.h
fsimage-p64.o: fsimage-p64.c archdep.h diskconstants.h diskimage.h \
 types.h p64.h p64config.h lib.h log.h fsimage-p64.h fsimage.h cbmdos.h \
 gcr.h util.h
fsimage-probe.o: fsimage-probe.c archdep.h diskconstants.h diskimage.h \
 types.h p64.h p64config.h lib.h log.h gcr.h cbmdos.h fsimage-gcr.h \
 fsimage-p64.h fsimage-probe.h fsimage.h util.h x64.h
gcr.o: gcr.c gcr.h types.h cbmdos.h lib.h log.h diskimage.h archdep.h \
 p64.h p64config.h
imagecontents.o: imagecontents.c charset.h types.h diskcontents.h \
 imagecontents.h lib.h log.h util.h archdep.h
lib.o: lib.c types.h log.h lib.h
log.o: log.c archdep.h lib.h types.h log.h util.h
p64.o: p64.c p64.h p64config.h lib.h types.h log.h
rawfile.o: rawfile.c archdep.h fileio.h types.h lib.h log.h util.h \
 rawfile.h
util.o: util.c archdep.h lib.h types.h log.h util.h
vdrive.o: vdrive.c archdep.h cbmdos.h types.h diskconstants.h diskimage.h \
 p64.h p64config.h lib.h log.h vdrive-bam.h vdrive-command.h vdrive-dir.h \
 vdrive-iec.h vdrive-internal.h vdrive-rel.h vdrive.h
vdrive-bam.o: vdrive-bam.c cbmdos.h types.h diskconstants.h diskimage.h \
 archdep.h p64.h p64config.h lib.h log.h vdrive-bam.h vdrive-command.h \
 vdrive.h vdrive-dir.h
vdrive-command.o: vdrive-command.c cbmdos.h types.h diskimage.h archdep.h \
 p64.h p64config.h lib.h log.h util.h vdrive-bam.h vdrive-command.h \
 vdrive-dir.h vdrive-iec.h vdrive-rel.h vdrive.h diskconstants.h
vdrive-dir.o: vdrive-dir.c cbmdos.h types.h diskconstants.h diskimage.h \
 archdep.h p64.h p64config.h lib.h log.h vdrive-bam.h vdrive-dir.h \
 vdrive-iec.h vdrive-rel.h vdrive.h
vdrive-iec.o: vdrive-iec.c archdep.h cbmdos.h types.h diskimage.h p64.h \
 p64config.h lib.h log.h vdrive-bam.h vdrive-command.h vdrive-dir.h \
 vdrive-iec.h vdrive-rel.h vdrive.h diskconstants.h
vdrive-internal.o: vdrive-internal.c cbmdos.h types.h diskimage.h \
 archdep.h p64.h p64config.h lib.h log.h vdrive-command.h \
 vdrive-internal.h vdrive.h vdrive-dir.h
vdrive-rel.o: vdrive-rel.c cbmdos.h types.h diskimage.h archdep.h p64.h \
 p64config.h lib.h log.h vdrive-bam.h vdrive-command.h vdrive-dir.h \
 vdrive-iec.h vdrive-rel.h vdrive.h
zfile.o: zfile.c archdep.h lib.h types.h log.h util.h zfile.h miniz.h
archdep-arduino.o: archdep-arduino.cpp
archdep-meatloaf.o: archdep-meatloaf.cpp
main.oo: main.cpp VDriveClass.h
VDriveClass.oo: VDriveClass.cpp VDriveClass.h lib.h types.h log.h util.h \
 archdep.h charset.h fileio.h vdrive.h vdrive-dir.h cbmdos.h \
 vdrive-command.h vdrive-iec.h cbmimage.h diskimage.h p64.h p64config.h \
 diskcontents.h diskcontents-block.h imagecontents.h
