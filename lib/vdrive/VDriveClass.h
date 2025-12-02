#ifndef VDRIVE_CLASS
#define VDRIVE_CLASS

#include <inttypes.h>
#include <stddef.h>

struct vdrive_s;

class VDrive
{
 public:
  VDrive(uint8_t unit);
  ~VDrive();

  // creates a new VDrive using disk image in "imagefile", returns NULL
  // if the image cannot be opened
  static VDrive *create(uint8_t unit, const char *imagefile, bool readOnly = false);

  // opens a new disk image on the host file system, using the archdep_* functions
  // to interact with the file system
  bool openDiskImage(const char *filename, bool readOnly = false);

  // returns the filename of the currently open disk image (NULL if nothing opened)
  const char *getDiskImageFilename();

  // close the disk image currently in use
  void closeDiskImage();

  // return true if a disk image has been successfuly opened for this drive
  bool isOk();

  // open a file within the current disk image on the given channel
  // nameLen is the length of the file name in bytes, if -1 then assume null-terminated name
  // if convertToPETSCII is true convert name from ASCII to PETSCII encoding before opening
  bool openFile(uint8_t channel, const char *name, int nameLen = -1, bool convertNameToPETSCII = false);

  // close the file that is currently open on a channel (if any)
  bool closeFile(uint8_t channel);

  // close all currently open files on all channels
  void closeAllChannels();

  // return the number of currently active channels
  int getNumOpenChannels() { return m_numOpenChannels; }

  // prints a directory listing of the disk image to the log
  void printDir();

  // return true if the file on the given channel is ok to read and/or write
  bool isFileOk(uint8_t channel);

  // read data from the file on the given channel. On entry, nbytes should contain
  // the maximum number of bytes to read, on exit, nbytes contains the number of bytes
  // actually read (can be different due to error or EOF).
  // Returns false if an error occurred while reading (EOF is not an error) 
  // and true otherwise.
  // The "eoi" parameter will be set to "true" if an EOF condition was encountered
  // during the current call, otherwise it will remain unchanged.
  bool read(uint8_t channel, uint8_t *buffer, size_t *nbytes, bool *eoi);

  // write data to the file on the given channel. On entry, nbytes should contain
  // the number of bytes to write, on exit, nbytes contains the number of bytes
  // actually written (can be different due to error).
  // Returns false if an error occurred while writing and true otherwise.
  bool write(uint8_t channel, uint8_t *buffer, size_t *nbytes);

  // execute the given DOS command, returns true on success false otherwise
  // returns 0 if there was an error (call getStatusString)
  //         1 if the command succeeded
  //         2 if the command succeeded AND there is return data in the status buffer (M-R)
  //           (call getStatusBuffer to retrieve)
  int execute(const char *cmd, size_t cmdLen, bool convertToPETSCII = false);

  // returns the current status message from the error message buffer
  // calling this if read/write/execute fails gives the standard CBMDOS error messages
  const char *getStatusString();

  // returns the current status code according to the error message buffer
  // returns -1 if the content of the error message buffer does not start with "NN," (N=digit)
  int getStatusCode();

  // copies the contents of the drive's status buffer to "buf", not exceeding
  // the given bufSize length. If given, "eoi" will be set to true/false
  // depending on whether all information from the buffer has been read
  size_t getStatusBuffer(void *buf, size_t bufSize, bool *eoi = NULL);

  // read sector data from the disk image and place it in "buf"
  // "buf" must have a size of at least 256 bytes
  bool readSector(uint32_t track, uint32_t sector, uint8_t *buf);

  // write data from "buf" into a sector on the disk image
  // "buf" must have a size of at least 256 bytes
  bool writeSector(uint32_t track, uint32_t sector, const uint8_t *buf);

  // create and optionally format a new disk image. Parameters
  // - filename: name of the created image file on the host file system, required
  // - itype: image type ("d64", "g64", ...), if NULL use extension from filename parameter
  // - name: disk name and id ("NAME,ID") used when formatting the disk image
  //         * if NULL, disk image will not be formatted
  //         * if ",ID" is missing then "00" will be used as ID
  //  - convertNameToPETSCII: if false then the name argument is assumed to be PETSCII,
  //    otherwise it will be converted from ASCII to PETSCII
  static bool createDiskImage(const char *filename, const char *itype, const char *name, bool convertNameToPETSCII);

 private:
  void countOpenChannels();

  int m_numOpenChannels;
  struct vdrive_s *m_drive;
};

#endif
