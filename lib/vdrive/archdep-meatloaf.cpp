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
#define DBG(x) Debug_printv  x
#else
#define DBG(x)
#endif

#define GET_MSTREAM(f) (((std::shared_ptr<MStream> *) f)->get())


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


off_t archdep_file_size(ADFILE *file)
{
  uint32_t s;

  s = GET_MSTREAM(file)->size();
  DBG(("archdep_file_size: %p %li", file, s));

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
  ADFILE *res = NULL;

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

      // f->getSourceStream() returns a std::shared_ptr. We have to pass the ADFILE
      // through the C code portions of the vdrive implementation which cannot deal
      // with pointers to C++ templates.
      // We cannot create a copy of the MStream object since MStream does not have
      // a copy constructor. If we just use the MStream pointer then the underlying
      // object gets deleted once the last shared_ptr is deleted, causing crashes.
      // So we have to create a pointer to a std::shared_ptr<MStream> object.
      res = (ADFILE *) (new std::shared_ptr<MStream>(f->getSourceStream(omode)));

      DBG(("archdep_fopen: file=%p, mode=%lu", res, (unsigned long) omode));

      delete f;
    }
else
  { DBG(("archdep_fopen: cannot open file")); }

  return res;
}


int archdep_fclose(ADFILE *file)
{
  DBG(("archdep_fclose: %p", file));
  delete (std::shared_ptr<MStream> *) file;
  return 0;
}


size_t archdep_fread(void* buffer, size_t size, size_t count, ADFILE *file)
{
  DBG(("archdep_fread: %p %u %u", file, size, count));

  MStream *s = GET_MSTREAM(file);
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


size_t archdep_fwrite(const void* buffer, size_t size, size_t count, ADFILE *file)
{
  DBG(("archdep_fwrite: %p %u %u", file, size, count));

  MStream *s = GET_MSTREAM(file);
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


long int archdep_ftell(ADFILE *file)
{
  DBG(("archdep_ftell: %p", file));
  long int res = (long int) GET_MSTREAM(file)->position();
  DBG(("=> %li", res));
  return res;
}


int archdep_fseek(ADFILE *file, long int offset, int whence)
{
  DBG(("archdep_fseek: %p %i %li", file, whence, offset));
  int res = GET_MSTREAM(file)->seek(offset, whence) ? 0 : -1;
  DBG(("=> %i %lu", res, GET_MSTREAM(file)->position()));
  return res;
}


int archdep_fflush(ADFILE *file)
{
  return 0;
}


void archdep_frewind(ADFILE *file)
{
  archdep_fseek(file, 0, SEEK_SET);
}


int archdep_fisopen(ADFILE *file)
{
  return file!=NULL && GET_MSTREAM(file)!=NULL;
}


int archdep_fissame(ADFILE *file1, ADFILE *file2)
{
  DBG(("archdep_fissame: %p %p", file1, file2));
  return file1==file2;
}


int archdep_ferror(ADFILE *file)
{
  int res = (int) GET_MSTREAM(file)->error();
  DBG(("archdep_ferror: %p %i", file, res));
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
