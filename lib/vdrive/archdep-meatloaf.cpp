#if !defined(ARDUINO) && defined(ESP_PLATFORM)

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include "archdep.h"
#include "../meatloaf/meatloaf.h"
#include "../../include/debug.h"

extern "C"
{
#include "lib.h"
}

//#define DEBUG_ARCHDEP


#ifdef DEBUG_ARCHDEP
#define DBG(x) log_printf_vdrive  x
#else
#define DBG(x)
#endif


int archdep_default_logger(const char *level_string, const char *txt)
{
  Debug_printv("%s", txt);
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
  time_t timep;
  struct tm *ts;

  time(&timep);
  ts = localtime(&timep);
 
  ats->tm_wday = ts->tm_wday;
  ats->tm_year = ts->tm_year;
  ats->tm_mon  = ts->tm_mon;
  ats->tm_mday = ts->tm_mday;
  ats->tm_hour = ts->tm_hour;
  ats->tm_min  = ts->tm_min;
  ats->tm_sec  = ts->tm_sec;

  return ats;
}


int archdep_access(const char *pathname, int mode)
{
  int res = 0;

  MFile *f = MFSOwner::File(pathname);
  if( f )
    {
      if( mode==ARCHDEP_F_OK )
        res = f->exists() ? 0 : -1;
      else 
        {
          if( (mode & ARCHDEP_W_OK) && !f->isWritable )
            res = -1;
          if( (mode & ARCHDEP_X_OK) )
            res = -1;
        }

      delete f;
    }
  else
    res = -1;

  return res;
}


int archdep_stat(const char *filename, size_t *len, unsigned int *isdir)
{
  int res = 0;

  MFile *f = MFSOwner::File(filename);
  if( f )
    {
      if( len!=NULL   ) *len = f->size;
      if( isdir!=NULL ) *isdir = f->isDirectory();
      res = 0;
      delete f;
    }
  else
    res = -1;

  return res;
}


bool archdep_file_exists(const char *path)
{
  return archdep_access(path, ARCHDEP_F_OK)==0;
}


char *archdep_tmpnam()
{
  return lib_strdup("//tmpfile");
}


off_t archdep_file_size(ADFILE *stream)
{
  uint32_t s;

  s = ((MStream *) stream)->size();
  DBG(("archdep_file_size: %p %li", stream, s));

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

  MFile *f = MFSOwner::File(path);
  if( f )
    {
      if( f->exists() && f->remove() )
        res = 0;

      delete f;
    }

  return res;
}


int archdep_rename(const char *oldpath, const char *newpath)
{
  int res = -1;

  MFile *f = MFSOwner::File(oldpath);
  if( f )
    {
      if( f->exists() && f->rename(newpath) )
        res = 0;

      delete f;
    }

  return res;
}


ADFILE *archdep_fnofile()
{
  return NULL;
}


ADFILE *archdep_fopen(const char* filename, const char* mode)
{
  MStream *res = NULL;

  DBG(("archdep_fopen: %s %s", filename, mode));

  MFile *f = MFSOwner::File(filename);
  if( f )
    {
      DBG(("archdep_fopen: file opened"));

      std::ios_base::openmode omode = std::ios_base::in;
      if( strcmp(mode, MODE_READ)==0 || strcmp(mode, MODE_READ_TEXT)==0  )
        omode = std::ios_base::in;
      else if( strcmp(mode, MODE_WRITE)==0 || strcmp(mode, MODE_WRITE_TEXT)==0  )
        omode = std::ios_base::out;
      else if( strcmp(mode, MODE_READ_WRITE)==0  )
        omode = (std::ios_base::in | std::ios_base::out);
      else if( strcmp(mode, MODE_APPEND)==0  )
        omode = (std::ios_base::in | std::ios_base::app);
      else if( strcmp(mode, MODE_APPEND_READ_WRITE)==0  )
        omode = (std::ios_base::in | std::ios_base::out | std::ios_base::app);

      res = f->getSourceStream(omode);
      DBG(("archdep_fopen: stream=%p, mode=%lu", res, (unsigned long) omode));

      delete f;
    }

  return res;
}


int archdep_fclose(ADFILE *stream)
{
  DBG(("archdep_fclose: %p", stream));
  delete (MStream *) stream;
  return 0;
}


size_t archdep_fread(void* buffer, size_t size, size_t count, ADFILE *stream)
{
  DBG(("archdep_fread: %p %u %u", stream, size, count));

  MStream *s = (MStream *) stream;
  size_t pos = 0;
  count = size*count;
  while( count>0 && s->available()>0 && s->error()==0 )
    {
      size_t n = s->read(((uint8_t *) buffer)+pos, count);
      count -= n;
      pos += n;
    }

  DBG(("=> %i %u %u", s->error(), pos, pos/size));
  return pos/size;
}


size_t archdep_fwrite(const void* buffer, size_t size, size_t count, ADFILE *stream)
{
  DBG(("archdep_fwrite: %p %u %u", stream, size, count));

  MStream *s = (MStream *) stream;
  size_t pos = 0;
  count = size*count;
  while( count>0 && s->error()==0 )
    {
      size_t n = s->write(((uint8_t *) buffer)+pos, count);
      count -= n;
      pos += n;
    }

  DBG(("=> %i %u %i", s->error(), pos, pos/size));
  return pos/size;
}


long int archdep_ftell(ADFILE *stream)
{
  DBG(("archdep_ftell: %p", stream));
  long int res = (long int) ((MStream *) stream)->position();
  DBG(("=> %li", res));
  return res;
}


int archdep_fseek(ADFILE *stream, long int offset, int whence)
{
  DBG(("archdep_fseek: %p %li %i", stream, whence, offset));
  int res = ((MStream *) stream)->seek(offset, whence) ? 0 : -1;
  DBG(("=> %i %lu", res, ((MStream *) stream)->position()));
  return res;
}


int archdep_fflush(ADFILE *stream)
{
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


int archdep_ferror(ADFILE *stream)
{
  int res = (int) ((MStream *) stream)->error();
  DBG(("archdep_ferror: %p %i", stream, res));
  return res;
}


void archdep_exit(int excode)
{
  Debug_printv("------------ EXIT -------------");
  while(1);
}


void archdep_init()
{
}

#endif
