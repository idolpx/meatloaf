#include <iostream>
#include "unity.h"

#include <dirent.h>
#include <sys/stat.h>

// #include "meat_io.h"
// #include "meat_stream.h"
// #include "meat_buffer.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_meatloaf_mfile_directory(void)
{
    std::string path = ".";

    DIR *dir;
    struct dirent *ent;
    struct stat st;

    if ((dir = opendir ( path.c_str() )) != NULL) {
        /* print all the files and directories within directory */
        while ((ent = readdir (dir)) != NULL) {
            stat(ent->d_name, &st);
            printf ("%8lld %-30s %8hu\r\n", st.st_size, ent->d_name, st.st_mode);
        }
        closedir (dir);
    } else {
        /* could not open directory */
        printf ("error");
    }
}

void test_meatloaf_mfile_properties(void)
{
    // std::unique_ptr<MFile> testMFile = Meat::New<MFile>("http://c64.meatloaf.cc/fb64");

    // printf("\n\n* %s File properties\r\n", testMFile->url.c_str());
    // printf("Url: %s, isDir = %d\r\n", testMFile->url.c_str(), testMFile->isDirectory());
    // printf("Scheme: [%s]\r\n", testMFile->scheme.c_str());
    // printf("Username: [%s]\r\n", testMFile->user.c_str());
    // printf("Password: [%s]\r\n", testMFile->pass.c_str());
    // printf("Host: [%s]\r\n", testMFile->host.c_str());
    // printf("Port: [%s]\r\n", testMFile->port.c_str());    
    // printf("Path: [%s]\r\n", testMFile->path.c_str());

    // if ( testMFile->streamFile )
    //     printf("stream src: [%s]\r\n", testMFile->streamFile->url.c_str());

    // printf("path in stream: [%s]\r\n", testMFile->pathInStream.c_str());
    // printf("File: [%s]\r\n", testMFile->name.c_str());
    // printf("Extension: [%s]\r\n", testMFile->extension.c_str());
    // printf("Size: [%d]\r\n", testMFile->size());
    // printf("Is text: [%d]\r\n", testMFile->isText());
    // printf("-------------------------------\r\n");
}

void process()
{
    UNITY_BEGIN();

    RUN_TEST(test_meatloaf_mfile_directory);
    RUN_TEST(test_meatloaf_mfile_properties);

    UNITY_END();
}

int main(int argc, char **argv)
{
    process();
}