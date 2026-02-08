#if !defined(ARDUINO) && defined(__GNUC__)

#include <stdio.h>
#include <string.h>
#include <cstdlib>

#include "VDriveClass.h"


int main_(VDrive *drive, int argc, char **argv)
{
  printf("CMD: ");
  for(int i=2; i<argc; i++) printf("%s ", argv[i]);
  printf("\n");

  if( argc>1 )
    {
      if( argc>2 && (strcmp(argv[2], "list")==0 || strcmp(argv[2], "dir")==0) )
        drive->printDir();
      else if( argc>2 && strcmp(argv[2], "create")==0 )
        {
          char name[17];
          if( argc>3 )
            strncpy(name, argv[3], 16);
          else
            {
              char *dot = strchr(argv[1], '.');
              if( dot!=NULL ) strncpy(name, argv[1], (dot-argv[1])<16 ? (dot-argv[1]) : 16);
              name[16] = 0;
            }

          if( VDrive::createDiskImage(argv[1], NULL, name, true) )
            printf("Image %s created", argv[1]);
        }
      else if( argc>3 && strcmp(argv[2], "read")==0 )
        {
          for(int i=3; i<argc; i++)
            {
              //const char *testfile = "\xa0\x20\x20\x20\xa0\x20\xa0\x20";
              const char *testfile = "\x00";

              int channel = 2;
              printf("Reading file '%s' ...\n", argv[i]);
              bool ok;
              if( strcmp(argv[i], "test")==0 )
                ok = drive->openFile(channel, testfile, 1, false);
              else
                ok = drive->openFile(channel, argv[i], -1, true);

              if( ok )
                {
                  FILE *outfile = fopen(argv[i][0]==':' ? argv[i]+1 : argv[i], "wb");
                  if( outfile )
                    {
                      bool eof = false;
                      uint8_t buf[128];
                      while( drive->isFileOk(channel) && !eof )
                        {
                          size_t n = 128;
                          if( drive->read(channel, buf, &n, &eof) )
                            {
                              fwrite(buf, 1, n, outfile);
                            }
                          else
                            {
                              printf("Error (read): %s\n", drive->getStatusString());
                              break;
                            }
                        }

                      fclose(outfile);
                    }
                  else
                    printf("Can not write to local file: %s\n", argv[i]);

                  drive->closeFile(channel);
                }
              else
                printf("Error (open): %s\n", drive->getStatusString());
            }
        }
      else if( argc>3 && strcmp(argv[2], "write")==0 )
        {
          FILE *infile = fopen(argv[3], "rb");
          if( infile )
            {
              if( drive->openFile(1, argv[3], true) )
                {
                  uint8_t buf[128];
                  while( drive->isFileOk(1) && !feof(infile) )
                    {
                      size_t n = fread(buf, 1, 128, infile);
                      if( !drive->write(1, buf, &n) )
                        {
                          printf("Error (write): %s\n", drive->getStatusString());
                          break;
                        }
                    }
          
                  drive->closeFile(1);
                }
              else
                printf("Error (open): %s\n", drive->getStatusString());

              fclose(infile);
            }
          else
            printf("Can not read from local file: %s\n", argv[3]);
        }
      else if( argc>4 && strcmp(argv[2], "bread")==0 )
        {
          int channel = 2;
          char cmd[100];
          snprintf(cmd, 100, "U1 %i 0,%s,%s", channel, argv[3], argv[4]);
          if( drive->openFile(channel, "#") && (drive->execute(cmd, strlen(cmd)) || true) )
            {
              size_t n = 1;
              uint8_t b;
              bool eoi;
              for(int i=0; i<256; i++)
                {
                  if( !drive->read(channel, &b, &n, &eoi) )
                    printf("XX ");
                  else if( n==0 )
                    printf("-- ");
                  else
                    printf("%02X ", b);

                  if( (i%16)==15 ) printf("\n");
                }

              drive->closeFile(channel);
            }

          printf("Status: %s\n", drive->getStatusString());
        }
      else if( argc>4 && strcmp(argv[2], "bmread")==0 )
        {
          char buf[100];
          snprintf(buf, 100, "U1 8 0,%s,%s", argv[3], argv[4]);
          if( drive->openFile(8, "#2") && drive->execute(buf, strlen(buf)) && drive->execute("M-R\x00\x05\0x00", 6) )
            {
              bool eoi = false;
              while( !eoi )
                {
                  size_t len = drive->getStatusBuffer(buf, 40, &eoi);
                  for(int j=0; j<len; j++) printf("%02X ", (unsigned char) buf[j]);
                  printf("\n");
                }
              drive->closeFile(8);
            }

          printf("Status: %s\n", drive->getStatusString());
        }
      else if( argc>4 && strcmp(argv[2], "rread")==0 )
        {
          int channel = 2;
          if( drive->openFile(channel, argv[3], true) )
            {
              uint8_t buf[256];
              buf[0] = 'P';
              buf[1] = 96 + channel;
              buf[2] = atoi(argv[4]);
              buf[3] = 0;
              buf[4] = 1;
              if( drive->execute((const char *) buf, 5) )
                {
                  int n = argc>5 ? atoi(argv[5]) : 1;
                  for(int j=0; j<n; j++)
                    {
                      bool eoi = false;
                      while( !eoi )
                        {
                          size_t n = 1;
                          if( !drive->read(channel, buf, &n, &eoi) )
                            { printf("READ-ERROR\n"); break; }
                              
                          for(size_t i=0; i<n; i++) printf("%02X ", buf[i]);
                        }
                      printf("\n");
                    }
                }

              printf("Status: %s\n", drive->getStatusString());

              drive->closeFile(channel);
            }
        }
      else if( argc>5 && strcmp(argv[2], "bwrite")==0 )
        {
          if( drive->openFile(2, "#") )
            {
              size_t n = 256;
              uint8_t buf[256];
              memset(buf, atoi(argv[5]), 256);
              if( drive->write(2, buf, &n) && n==256 ) 
                {
                  char cmd[100];
                  snprintf(cmd, 100, "U2 2 0,%s,%s", argv[3], argv[4]);
                  drive->execute(cmd, strlen(cmd));
                }

              drive->closeFile(2);
            }

          printf("Status: %s\n", drive->getStatusString());
        }
      else if( argc>4 && strcmp(argv[2], "mread")==0 )
        {
          uint8_t buf[256];
          memcpy(buf, "M-R", 3);
          buf[3] = atoi(argv[3])&255;
          buf[4] = atoi(argv[3])/256;
          buf[5] = atoi(argv[4]);
          drive->execute((const char *) buf, 6);

          size_t len = drive->getStatusBuffer(buf, 256);
          printf("DATA:");
          for(int i=0; i<len; i++) printf(" %02X", buf[i]);
          printf("\n");
        }
      else if( argc>4 && strcmp(argv[2], "mwrite")==0 )
        {
          uint8_t buf[256];
          uint16_t addr = atoi(argv[3]), len = atoi(argv[4]);
          memcpy(buf, "M-W", 3);
          buf[3] = addr&255;
          buf[4] = addr/256;
          buf[5] = len;
          for(int i=0; i<len; i++) buf[6+i] = atoi(argv[5+i]);
          drive->execute((const char *) buf, 6+len);
        }
      else if( argc>2 && strcmp(argv[2], "@")==0 )
        {
          if( argc>3 ) drive->execute(argv[3], strlen(argv[3]), true);
          printf("Status: %s\n", drive->getStatusString());
        }
      else if( argc>2 && strcmp(argv[2], "dump")==0 )
        {
          char cmd[100];
          int channel = 3;
          int sectors[35] = {21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,19,19,19,19,19,19,19,18,18,18,18,18,18,17,17,17,17,17};
          if( drive->openFile(channel, "#") )
            {
              for(int t=1; t<=35; t++)
                for(int s=0; s<sectors[t-1]; s++)
                  {
                    printf("\nReading track %i sector %i\n", t, s);
                    snprintf(cmd, 100, "U1:%i,0,%i,%i", channel, t, s);
                    bool ok = drive->execute(cmd, strlen(cmd));
                    if( atoi(drive->getStatusString())>0 ) printf("Status: %s\n", drive->getStatusString());
                    int i=0;

                    size_t n = 1;
                    uint8_t b;
                    bool eoi;
                    for(int i=0; i<256; i++)
                      {
                        if( !drive->read(channel, &b, &n, &eoi) )
                          printf("XX ");
                        else if( n==0 )
                          printf("-- ");
                        else
                          printf("%02X ", b);

                        if( (i%16)==15 ) printf("\n");
                      }
                  }

              drive->closeFile(channel);
            }
        }
      else if( argc>2 && strcmp(argv[2], "nblocks")==0 )
        {
          printf("Number of blocks: %i\n", drive->getFileNumBlocks(argc>3 ? argv[3] : "*", true));
        }
      else
        printf("invalid command\n");
    }

  return 0;
}


int main(int argc, char **argv)
{
  VDrive *drive = new VDrive(0);
  
  char *argv2[256];

  if( argc>1 )
    {
      if( !drive->openDiskImage(argv[1]) )
        printf("Unable to open disk image: %s\n", argv[1]);
    }

  argv2[0]=argv[0];
  argv2[1]=argv[1];

  int cmdstart = 2;
  while( cmdstart<argc )
    {
      int i = 0;
      while( (cmdstart+i)<argc && strcmp(argv[cmdstart+i], ";")!=0 )
        { argv2[2+i] = argv[cmdstart+i]; i++; }

      main_(drive, 2+i, argv2);

      while( (cmdstart+i)<argc && strcmp(argv[cmdstart+i], ";")==0 ) i++;
      cmdstart += i;
    }

  delete drive;
  return 0;
}


#endif
