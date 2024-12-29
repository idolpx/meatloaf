#ifdef WIN32

#include <stdio.h>
#include <string.h>
#include <cstdlib>

#include "VDriveClass.h"


int main(int argc, char **argv)
{
  VDrive *drive = new VDrive(0);
  
  if( argc>1 )
    {
      if( argc>2 && strcmp(argv[2], "list")==0 && drive->openDiskImage(argv[1]) )
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
      else if( argc>3 && strcmp(argv[2], "read")==0 && drive->openDiskImage(argv[1]) )
        {
          if( drive->openFile(0, argv[3], true) )
            {
              FILE *outfile = fopen(argv[3], "wb");
              if( outfile )
                {
                  bool eof = false;
                  uint8_t buf[128];
                  while( drive->isFileOk(0) && !eof )
                    {
                      size_t n = 128;
                      if( drive->read(0, buf, &n, &eof) )
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
                printf("Can not write to local file: %s\n", argv[3]);

              drive->closeFile(0);
            }
          else
            printf("Error (open): %s\n", drive->getStatusString());
        }
      else if( argc>3 && strcmp(argv[2], "write")==0 && drive->openDiskImage(argv[1]) )
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
      else if( argc>4 && strcmp(argv[2], "bread")==0 && drive->openDiskImage(argv[1]) )
        {
          char cmd[100];
          snprintf(cmd, 100, "U1 2 0,%s,%s", argv[3], argv[4]);
          if( drive->openFile(2, "#") && drive->execute(cmd, strlen(cmd)) )
            {
              size_t n = 1;
              uint8_t b;
              bool eoi;
              while( !eoi )
                if( drive->read(2, &b, &n, &eoi) )
                  printf("%02X ", b);

              drive->closeFile(2);
              printf("\n");
            }

          printf("Status: %s\n", drive->getStatusString());
        }
      else if( argc>4 && strcmp(argv[2], "rread")==0 && drive->openDiskImage(argv[1]) )
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
      else if( argc>5 && strcmp(argv[2], "bwrite")==0 && drive->openDiskImage(argv[1]) )
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
      else if( argc>4 && strcmp(argv[2], "mread")==0 && drive->openDiskImage(argv[1]) )
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
      else if( argc>3 && strcmp(argv[2], "@")==0 && drive->openDiskImage(argv[1]) )
        {
          drive->execute(argv[3], strlen(argv[3]), true);
          printf("Status: %s\n", drive->getStatusString());
        }
      else
        printf("invalid command\n");
    }

  delete drive;

  return 0;
}

#endif
