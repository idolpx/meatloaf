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


void process()
{
    UNITY_BEGIN();

    RUN_TEST(test_meatloaf_mfile_directory);

    UNITY_END();
}

int main(int argc, char **argv)
{
    process();
}