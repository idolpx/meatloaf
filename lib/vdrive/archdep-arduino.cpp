#if defined(ARDUINO)

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <Arduino.h>
#include <SdFat.h>
#include "archdep.h"

extern "C"
{
#include "lib.h"
}

//#define DEBUG_ARCHDEP

#ifdef DEBUG_ARCHDEP
#define DBG(x) Serial.printf x
#else
#define DBG(x)
#endif


int archdep_default_logger(const char *level_string, const char *txt)
{
  if( Serial ) Serial.println(txt);
  return 0;
}


int archdep_default_logger_is_terminal(void)
{
  return 1;
}


int archdep_expand_path(char **return_path, const char *orig_name)
{
  *return_path = lib_strdup(orig_name);
  return 0;
}


archdep_tm_t *archdep_get_time(archdep_tm_t *ats)
{
  ats->tm_wday = 0;
  ats->tm_year = 70;
  ats->tm_mon  = 0;
  ats->tm_mday = 1;
  ats->tm_hour = 0;
  ats->tm_min  = 0;
  ats->tm_sec  = 0;
  return ats;
}


int archdep_access(const char *pathname, int mode)
{
  int res = -1;

  if( mode==ARCHDEP_F_OK )
    res = archdep_file_exists(pathname) ? 0 : -1;
  else if( mode & ARCHDEP_X_OK )
    res = -1;
  else if( mode & ARCHDEP_W_OK )
    {
      SdFile f;
      if( f.open(pathname, O_RDWR) )
        {
          res = f.isWritable() ? 0 : -1;
          f.close();
        }
    }
  else if( mode & ARCHDEP_R_OK )
    {
      SdFile f;
      if( f.open(pathname, O_RDONLY) )
        {
          res = 0;
          f.close();
        }
    }

  DBG(("archdep_access: %s %i %i\n", pathname, mode, res));
  return res;
}


int archdep_stat(const char *filename, size_t *len, unsigned int *isdir)
{
  int res = -1;

  SdFile f;
  if( f.open(filename, O_RDONLY) )
    {
      res = 0;
      if( len ) *len = f.fileSize();
      if( isdir ) *isdir = f.isDir() ? 1 : 0;
    }

  DBG(("archdep_stat: %s %i %i\n", filename, len ? *len : -1, isdir ? *isdir : -1));
  return res;
}


bool archdep_file_exists(const char *path)
{
  bool res = false;
  SdFile dir;
  if( dir.openCwd() )
    {
      res = dir.exists(path);
      dir.close();
    }

  DBG(("archdep_file_exists: %s %i\n", path, res));
  return res;
}


char *archdep_tmpnam()
{
  char *res = lib_strdup("tmpfile");
  DBG(("archdep_tmpnam: %s\n", res));
  return res;
}


off_t archdep_file_size(ADFILE *stream)
{
  uint32_t s = ((SdFile *) stream)->fileSize();
  DBG(("archdep_file_size: %p %u\n", stream, s));
  return (off_t) s;
}


archdep_dir_t *archdep_opendir(const char *path, int mode)
{
  return NULL;
}


const char *archdep_readdir(archdep_dir_t *dir)
{
  return NULL;
}


void archdep_closedir(archdep_dir_t *dir)
{
}


int archdep_remove(const char *path)
{
  int res = -1;
  SdFile dir;
  if( dir.openCwd() )
    {
      if( dir.remove(path) ) res = 0;
      dir.close();
    }
  
  DBG(("archdep_remove: %s %i\n", path, res));
  return res;
}


int archdep_rename(const char *oldpath, const char *newpath)
{
  return 0;
}


ADFILE *archdep_fnofile()
{
  return NULL;
}


ADFILE *archdep_fopen(const char* filename, const char* mode)
{
  ADFILE *res = NULL;
  DBG(("archdep_fopen: %s %s ", filename, mode));

  oflag_t flags = 0;
  if( strcmp(mode, MODE_READ)==0 || strcmp(mode, MODE_READ_TEXT)==0  )
    flags = O_RDONLY;
  else if( strcmp(mode, MODE_WRITE)==0 || strcmp(mode, MODE_WRITE_TEXT)==0  )
    flags = O_WRONLY | O_CREAT | O_TRUNC;
  else if( strcmp(mode, MODE_READ_WRITE)==0 )
    flags = O_RDWR;
  else if( strcmp(mode, MODE_APPEND)==0  )
    flags = O_WRITE | O_AT_END;
  else if( strcmp(mode, MODE_APPEND_READ_WRITE)==0  )
    flags = O_RDWR | O_AT_END;

  SdFile *f = new SdFile();
  if( f->open(filename, flags) )
    res = (ADFILE *) f;
  else
    delete f; 

  DBG(("=> %p\n", f));
  return res;
}


int archdep_fclose(ADFILE *file)
{
  DBG(("archdep_fclose: %p\n", file));
  SdFile *f = (SdFile *) file;
  if( f!=NULL ) { f->close(); delete f; }
  return 0;
}


size_t archdep_fread(void* buffer, size_t size, size_t count, ADFILE *stream)
{
  DBG(("archdep_fread: %p %u %u ", stream, size, count));
  size_t n = ((SdFile *) stream)->read(buffer, size*count);
  DBG(("=> %i %u %u\n", ((SdFile *) stream)->getError(), n, n/size));
  return n/size;
}


int archdep_fgetc(ADFILE *stream)
{
  return ((SdFile *) stream)->read();
}


size_t archdep_fwrite(const void* buffer, size_t size, size_t count, ADFILE *stream)
{
  DBG(("archdep_write: %p %u %u ", stream, size, count));
  size_t n = ((SdFile *) stream)->write(buffer, size*count);
  DBG(("=> %i %u %u\n", ((SdFile *) stream)->getError(), n, n/size));
  return n/size;
}


long int archdep_ftell(ADFILE *stream)
{
  long int pos = ((SdFile *) stream)->curPosition();
  DBG(("archdep_ftell: %p %li\n", stream, pos));
  return pos;
}


int archdep_fseek(ADFILE *stream, long int offset, int whence)
{
  SdFile *f = (SdFile *) stream;
  DBG(("archdep_fseek: %p %li %i ", stream, whence, offset));

  switch( whence )
    {
    case SEEK_SET: f->seekSet(offset); break;
    case SEEK_CUR: f->seekCur(offset); break;
    case SEEK_END: f->seekEnd(offset); break;
    }

  DBG(("=> %i %lu\n", 0, f->curPosition()));
  return 0;
}


int archdep_fflush(ADFILE *stream)
{
  //((SdFile *) stream)->flush();
  return 0;
}


void archdep_frewind(ADFILE *file)
{
  archdep_fseek(file, 0, SEEK_SET);
}


int archdep_fisopen(ADFILE *file)
{
  return file!=NULL;
}


int archdep_fissame(ADFILE *file1, ADFILE *file2)
{
  return (file1==file2);
}


int archdep_ferror(ADFILE *file)
{
  return ((SdFile *) file)->getError();
}


void archdep_exit(int excode)
{
  Serial.println("------------ EXIT -------------");
  while(1);
}


void archdep_init()
{
}

#endif
