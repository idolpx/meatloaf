#include "VDriveClass.h"

extern "C" 
{
#include "util.h"
#include "charset.h"
#include "vdrive.h"
#include "vdrive-command.h"
#include "vdrive-iec.h"
#include "cbmimage.h"
#include "diskimage.h"
#include "diskcontents-block.h"
#include "imagecontents.h"
#include "fsimage.h"
#include <ctype.h>
}


VDrive::VDrive(uint8_t unit)
{
  m_drive = (vdrive_t *) lib_calloc(1, sizeof(struct vdrive_s));
  vdrive_device_setup(m_drive, unit);
  m_numOpenChannels = 0;
}


VDrive::~VDrive()
{
  if( m_drive->image!=NULL ) closeDiskImage();
  vdrive_device_shutdown(m_drive);
  lib_free(m_drive);
}


VDrive *VDrive::create(uint8_t unit, const char *imagefile, bool readOnly)
{
  VDrive *drive = new VDrive(unit);

  if( drive!=NULL && !drive->openDiskImage(imagefile, readOnly) )
    { delete drive; drive = NULL; }

  return drive;
}


bool VDrive::openDiskImage(const char *name, bool readOnly)
{
  disk_image_t *image;

  if( m_drive->image!=NULL )
    closeDiskImage();

  image = disk_image_create();
  image->device = DISK_IMAGE_DEVICE_FS;
  disk_image_media_create(image);

  image->gcr = NULL;
  image->p64 = (PP64Image)lib_calloc(1, sizeof(TP64Image));
  P64ImageCreate((PP64Image)image->p64);
  
  if( readOnly )
    image->read_only = 1;
  else
    image->read_only = archdep_access(name, ARCHDEP_W_OK)==0 ? 0 : 1;

  disk_image_name_set(image, name);
  if (disk_image_open(image) < 0) 
    {
      P64ImageDestroy((PP64Image)image->p64);
      lib_free(image->p64);
      disk_image_media_destroy(image);
      disk_image_destroy(image);
      return false;
    }
  
  return vdrive_attach_image(image, m_drive->unit, 0, m_drive)==0;
}


void VDrive::closeDiskImage()
{
  disk_image_t *image = m_drive->image;

  if (image != NULL) 
    {
      vdrive_close_all_channels(m_drive);
      vdrive_detach_image(image, m_drive->unit, 0, m_drive);
      P64ImageDestroy((PP64Image)image->p64);
      lib_free(image->p64);
      disk_image_close(image);
      disk_image_media_destroy(image);
      disk_image_destroy(image);
      m_drive->image = NULL;
      m_numOpenChannels = 0;
    }
}


bool VDrive::createDiskImage(const char *filename, const char *itype, const char *name, bool convertNameToPETSCII)
{
  char *command, *itypec = NULL;
  unsigned int disk_type;

  if( itype==NULL )
    {
      const char *s = strchr(filename, '.');
      itypec = strdup(s==NULL ? "" : s+1);
    }
  else
    itypec = lib_strdup(itype);

  for(char *c=itypec; *c!=0; c++) *c = util_tolower(*c);
  if (strcmp(itypec, "d64") == 0) {
    disk_type = DISK_IMAGE_TYPE_D64;
  } else if (strcmp(itypec, "d67") == 0) {
    disk_type = DISK_IMAGE_TYPE_D67;
  } else if (strcmp(itypec, "d71") == 0) {
    disk_type = DISK_IMAGE_TYPE_D71;
  } else if (strcmp(itypec, "d81") == 0) {
    disk_type = DISK_IMAGE_TYPE_D81;
  } else if (strcmp(itypec, "d80") == 0) {
    disk_type = DISK_IMAGE_TYPE_D80;
  } else if (strcmp(itypec, "d82") == 0) {
    disk_type = DISK_IMAGE_TYPE_D82;
  } else if (strcmp(itypec, "g64") == 0) {
    disk_type = DISK_IMAGE_TYPE_G64;
  } else if (strcmp(itypec, "g71") == 0) {
    disk_type = DISK_IMAGE_TYPE_G71;
#ifdef HAVE_X64_IMAGE
  } else if (strcmp(itypec, "x64") == 0) {
    disk_type = DISK_IMAGE_TYPE_X64;
#endif
  } else if (strcmp(itypec, "d1m") == 0) {
    disk_type = DISK_IMAGE_TYPE_D1M;
  } else if (strcmp(itypec, "d2m") == 0) {
    disk_type = DISK_IMAGE_TYPE_D2M;
  } else if (strcmp(itypec, "d4m") == 0) {
    disk_type = DISK_IMAGE_TYPE_D4M;
  } else if (strcmp(itypec, "d90") == 0) {
    disk_type = DISK_IMAGE_TYPE_D90;
  } else {
    lib_free(itypec);
    return false;
  }
  lib_free(itypec);

  if( cbmimage_create_image(filename, disk_type) < 0 )
    return false;

  if( name==NULL )
    return true;
  else
    {
      bool res = false;

      VDrive *drive = VDrive::create(0, filename);
      if( drive!=NULL )
        {
          const char *id = strchr(name, ',');
          command = util_concat(convertNameToPETSCII ? "n:" : "N:", name, id==NULL ? ",00" : id+1, NULL);
          res = drive->execute(command, strlen(command), convertNameToPETSCII);
          delete drive;
        }
      
      return res;
    }
}


const char *VDrive::getDiskImageFilename()
{
  if( m_drive->image!=NULL && m_drive->image->media.fsimage!=NULL )
    return m_drive->image->media.fsimage->name;
  else
    return NULL;
}


bool VDrive::isOk()
{
  return m_drive->image != NULL;
}


bool VDrive::openFile(uint8_t channel, const char *name, int nameLen, bool convertNameToPETSCII)
{
  bool res = false;

  // vdrive_iec_open returns a "NO CHANNEL" error if the channel is already open by Commodore
  // drives don't do that. They just re-open the channel with the new file name.
  if( m_drive->buffers[channel].mode!=BUFFER_NOT_IN_USE )
    closeFile(channel);

  if( nameLen<0 ) nameLen = strlen(name);

  if( convertNameToPETSCII )
    {
      char *pname = lib_strdup(name);
      charset_petconvstring((uint8_t *)pname, CONVERT_TO_PETSCII);
      res = vdrive_iec_open(m_drive, (uint8_t *) pname, (unsigned int) nameLen, channel, NULL)==0;
      lib_free(pname);
    }
  else
    res = vdrive_iec_open(m_drive, (uint8_t *) name, (unsigned int) nameLen, channel, NULL)==0;

  countOpenChannels();
  return res;
}


bool VDrive::closeFile(uint8_t channel)
{
  bool res = vdrive_iec_close(m_drive, channel)==SERIAL_OK;
  countOpenChannels();
  return res;
}


void VDrive::closeAllChannels()
{
  vdrive_close_all_channels(m_drive);
  m_numOpenChannels = 0;
}


bool VDrive::isFileOk(uint8_t channel)
{
  return m_drive->buffers[channel].mode!=BUFFER_NOT_IN_USE;
}


bool VDrive::read(uint8_t channel, uint8_t *buffer, size_t *nbytes, bool *eoi)
{
  size_t i = 0;
  while( i<*nbytes && m_drive->buffers[channel].readmode != CBMDOS_FAM_EOF )
    {
      int status = vdrive_iec_read(m_drive, buffer+i, channel);
      if( status==SERIAL_OK ) 
        i++;
      else if( status==SERIAL_EOF )
        {
          // SERIAL_EOF means that this was the last byte of data.
          // Note that SERIAL_EOF may happen for the last byte of
          // data requested. So the calling function can NOT just
          // determine an EOI condition by checking whether fewer 
          // bytes were returned than requested.
          if( eoi!=NULL ) *eoi = true;
          *nbytes = i+1;
          return true;
        }
      else
        {
          // neither SERIAL_OK nor SERIAL_EOF => error
          *nbytes = i;
          return false;
        }
    }
  
  *nbytes = i;
  return true;
}


bool VDrive::write(uint8_t channel, uint8_t *buffer, size_t *nbytes)
{
  size_t i = 0;
  while( i<*nbytes )
    {
      int status = vdrive_iec_write(m_drive, buffer[i], channel);
      if( status==SERIAL_OK )
        i++;
      else
        {
          *nbytes = i;
          return false;
        }
    }

  return true;
}


void VDrive::printDir()
{
  image_contents_t *listing = diskcontents_block_read(m_drive, 0);

  if( listing != NULL )
    {
      char *string = image_contents_to_string(listing, IMAGE_CONTENTS_STRING_ASCII);
      image_contents_file_list_t *element = listing->file_list;
      log_printf_vdrive("%s", string);
      lib_free(string);

      while( element )
        {
          string = image_contents_file_to_string(element, IMAGE_CONTENTS_STRING_ASCII);
          log_printf_vdrive("%s", string);
          lib_free(string);
          element = element->next;
        }

      if (listing->blocks_free >= 0) 
        {
          log_printf_vdrive("%d blocks free.", listing->blocks_free);
          fflush(stdout);
        }

      image_contents_destroy(listing);
    }
}


const char *VDrive::getStatusString()
{
  return (char *)m_drive->buffers[15].buffer;
}


int VDrive::getStatusCode()
{
  if( m_drive->buffers[15].length>=2 && 
      isdigit(m_drive->buffers[15].buffer[0]) &&
      isdigit(m_drive->buffers[15].buffer[1]) &&
      m_drive->buffers[15].buffer[2]==',' )
    return (m_drive->buffers[15].buffer[0]-'0')*10 + (m_drive->buffers[15].buffer[1]-'0');
  else
    return -1;
}


size_t VDrive::getStatusBuffer(void *buf, size_t bufSize, bool *eoi)
{
  // buffer[].length points to the last byte instead of giving the true length
  size_t len = m_drive->buffers[15].length+1;
  if( bufSize<len )
    {
      memcpy(buf, m_drive->buffers[15].buffer, bufSize);
      memmove(m_drive->buffers[15].buffer, m_drive->buffers[15].buffer+bufSize, len-bufSize);
      m_drive->buffers[15].length -= bufSize;
      if( eoi ) *eoi = false;
      return bufSize;
    }
  else
    {
      memcpy(buf, m_drive->buffers[15].buffer, len);
      vdrive_command_set_error(m_drive, 0, 0, 0);
      if( eoi ) *eoi = true;
      return len;
    }
}


int VDrive::execute(const char *cmd, size_t cmdLen, bool convertToPETSCII)
{
  char *pcmd = NULL;

  if( convertToPETSCII )
    {
      pcmd = lib_strdup(cmd);
      if( pcmd ) charset_petconvstring((uint8_t *)pcmd, CONVERT_TO_PETSCII);
    }

  int vres = vdrive_command_execute(m_drive, (uint8_t *) (pcmd==NULL ? cmd : pcmd), (unsigned int)cmdLen);
  if( pcmd!=NULL ) lib_free(pcmd);

  // some commands (e.g. "I") may close channels
  countOpenChannels();

  if( vres==0 )
    return 1;
  else if( vres<10 || vres==CBMDOS_IPE_DOS_VERSION )
    return 2;
  else
    return 0;
}


bool VDrive::readSector(uint32_t track, uint32_t sector, uint8_t *buf)
{
  return vdrive_read_sector(m_drive, buf, track, sector)==CBMDOS_IPE_OK;
}


bool VDrive::writeSector(uint32_t track, uint32_t sector, const uint8_t *buf)
{
  return vdrive_write_sector(m_drive, buf, track, sector)==CBMDOS_IPE_OK;
}


void VDrive::countOpenChannels()
{
  m_numOpenChannels = 0;
  for(uint8_t i=0; i<15; i++)
    if( m_drive->buffers[i].mode!=BUFFER_NOT_IN_USE )
      m_numOpenChannels++;
}
