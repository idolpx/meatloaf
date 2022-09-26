

#include <string>

#include <dirent.h>
#include <sys/stat.h>
#include <ArduinoJson.h>
//#include <archive_cpp.h>

#include "../include/global_defines.h"
#include "../include/debug.h"

#include "fnFsSD.h"
#include "fnWiFi.h"

#include "ml_tests.h"
#include "meat_io.h"
#include "meat_buffer.h"
#include "iec_host.h"
#include "../include/make_unique.h"
#include "basic_config.h"
#include "device_db.h"

#include "fnHttpClient.h"

//std::unique_ptr<MFile> m_mfile(MFSOwner::File(""));


void testHeader(std::string testName) {
    Debug_println("\n\n******************************");
    Debug_printf("* TESTING: %s\n", testName.c_str());
    Debug_println("******************************\n");
}

void testDiscoverDevices()
{
    iecHost iec;
    testHeader("Query Bus for Devices");
    for(size_t d=4; d<31; d++)
        iec.deviceExists(d);
}

// void testArchiveReader(std::string archive) {
//     // /* Test Line reader */
//     testHeader("C++ archive reader");

//     Debug_printf("* Trying to read archive: %s\n", archive.c_str());

//     auto readerStream = ArchiveReader(readeTest);
//     readerStream.open();

//     if(readerStream.is_open()) {
//         if(readerStream.eof()) {
//             Debug_printf("Reader returned EOF! :(");
//         }

//         while(!readerStream.eof()) {
//             std::string line;

//             readerStream >> line;

//             Debug_printf("%s\n",line.c_str());
//         };
//     }
//     else {
//         Debug_printf("*** ERROR: stream could not be opened!");
//     }
// }

void dumpFileProperties(MFile* testMFile) {
    Debug_printf("\n\n* %s File properties\n", testMFile->url.c_str());
    Debug_printf("Url: %s, isDir = %d\n", testMFile->url.c_str(), testMFile->isDirectory());
    Debug_printf("Scheme: [%s]\n", testMFile->scheme.c_str());
    Debug_printf("Username: [%s]\n", testMFile->user.c_str());
    Debug_printf("Password: [%s]\n", testMFile->pass.c_str());
    Debug_printf("Host: [%s]\n", testMFile->host.c_str());
    Debug_printf("Port: [%s]\n", testMFile->port.c_str());    
    Debug_printf("Path: [%s]\n", testMFile->path.c_str());

    if ( testMFile->streamFile )
        Debug_printf("stream src: [%s]\n", testMFile->streamFile->url.c_str());

    Debug_printf("path in stream: [%s]\n", testMFile->pathInStream.c_str());
    Debug_printf("File: [%s]\n", testMFile->name.c_str());
    Debug_printf("Extension: [%s]\n", testMFile->extension.c_str());
    Debug_printf("Size: [%d]\n", testMFile->size());
    Debug_printf("Is text: [%d]\n", testMFile->isText());
    Debug_printf("-------------------------------\n");
}

void testDirectory(MFile* dir, bool verbose=false) {
    testHeader("A directory");

    Debug_printf(" * Trying to list dir: %s\n", dir->url.c_str());

    if(!dir->isDirectory()) {
        Debug_printf("%s: Not a directory!", dir->url.c_str());
        return;
    }

    Debug_printf("* Listing %s\n", dir->url.c_str());
    Debug_printf("* pre get next file\n");
    auto e = dir->getNextFileInDir();
    Debug_printf("* past get next file\n");
    if(e != nullptr) {
        std::unique_ptr<MFile> entry(e);

        Debug_printf("* past creating file\n");


        while(entry != nullptr) {
            if(verbose)
                dumpFileProperties(entry.get());
            else
                Debug_printf("'%s'\n", entry->url.c_str());
            entry.reset(dir->getNextFileInDir());
        }
    }
    else {
        Debug_printf("* got nullptr - directory is empty?");
    }
}

void testDirectoryStandard(std::string path) {
    DIR *dir;
    struct dirent *ent;
    struct stat st;

    if ((dir = opendir ( path.c_str() )) != NULL) {
        /* print all the files and directories within directory */
        while ((ent = readdir (dir)) != NULL) {
            stat(ent->d_name, &st);
            Debug_printf ("%d %s %s\n", st.st_size, ent->d_name, S_ISREG(st.st_mode));
        }
        closedir (dir);
    } else {
        /* could not open directory */
        Debug_println ("error");
    }
}


void testRecursiveDir(MFile* file, std::string indent) {
    if(file->isDirectory()) {
        std::unique_ptr<MFile> entry(file->getNextFileInDir());

        while(entry != nullptr) {
            if(entry->isDirectory())
            {
                Debug_printf("%s%s <dir>\n", indent.c_str(), entry->name.c_str());
                testRecursiveDir(entry.get(), indent+"   ");
            }
            else
            {
                Debug_printf("%s%s\n", indent.c_str(), entry->name.c_str());                
            }

            entry.reset(file->getNextFileInDir());
        }
    }
}

void testCopy(MFile* srcFile, MFile* dstFile) {
    // testHeader("Copy file to destination");

    // Debug_printf("FROM:%s\nTO:%s\n", srcFile->url.c_str(), dstFile->url.c_str());

    // if(dstFile->exists()) {
    //     bool result = dstFile->remove();
    //     Debug_printf("FSTEST: %s existed, delete reult: %d\n", dstFile->path.c_str(), result);
    // }

    // srcFile->copyTo(dstFile);
}

void dumpParts(std::vector<std::string> v) {
    for(auto i = v.begin(); i < v.end(); i++)
        Debug_printf("%s::",(*i).c_str());
}

void testPaths(MFile* testFile, std::string subDir) {
    testHeader("Path ops");
    //std::shared_ptr<MFile> testFile(MFSOwner::File("http://somneserver.com/utilities/disk tools/cie.dnp/subdir/CIE+SERIAL"));
    dumpFileProperties(testFile);

    Debug_printf("We are in: %s\n",testFile->url.c_str());

    std::unique_ptr<MFile> inDnp(testFile->cd("/"+subDir));
    Debug_printf("- cd /%s = '%s'\n", subDir.c_str(), inDnp->url.c_str());

    std::unique_ptr<MFile> inFlash(testFile->cd("//"+subDir));
    Debug_printf("- cd //%s = '%s'\n", subDir.c_str(), inFlash->url.c_str());

    std::unique_ptr<MFile> parallel(testFile->cd("../"+subDir));
    Debug_printf("- cd ../%s = '%s'\n", subDir.c_str(), parallel->url.c_str());

    std::unique_ptr<MFile> inCie(testFile->cd(subDir));
    Debug_printf("- cd %s = '%s'\n", subDir.c_str(), inCie->url.c_str());
}

void testIsDirectory() {
    std::unique_ptr<MFile> testDir(MFSOwner::File("/NOTADIR/"));
    Debug_printf("dir [%s] exists [%d]\n",testDir->url.c_str(), testDir->isDirectory());
    testDir.reset(MFSOwner::File("/.sys"));
    Debug_printf("dir [%s] exists [%d]\n",testDir->url.c_str(), testDir->isDirectory());
    testDir.reset(MFSOwner::File("/.sys/"));
    Debug_printf("dir [%s] exists [%d]\n",testDir->url.c_str(), testDir->isDirectory());
}

void testUrlParser() {
    // Local File System
    testDirectory(MFSOwner::File("CCGMS"), true);
    testDirectory(MFSOwner::File("GAMES"), true);
    testDirectory(MFSOwner::File("/GAMES"), true);

    // URL
    testDirectory(MFSOwner::File("CS:/"), true);
    testDirectory(MFSOwner::File("CS://"), true);
    testDirectory(MFSOwner::File("HTTP://GOOGLE.COM"), true);
    testDirectory(MFSOwner::File("ML://C64.MEATLOAF.CC"), true);

    // Network Protocol
    testDirectory(MFSOwner::File("WS://GS.MEATLOAF.CC"), true);

    // Special File Type
    testDirectory(MFSOwner::File("C64.ML.CC.URL"), true);
}

void testCD() {
    std::unique_ptr<MFile> testDir(MFSOwner::File(""));

    Debug_println("A chain of CDs");
    // make a folder called GAMES on root of flash
    //testDir.reset(MFSOwner::File("/"));
    Debug_printf("I'm in %s, changing to GAMES\n", testDir->url.c_str());
    // then on the 64   LOAD"CD:GAMES",8
    testDir.reset(testDir->cd("GAMES"));
    Debug_printf("I'm in %s, changing to _\n", testDir->url.c_str());
    // then LOAD"CD_",8
    testDir.reset(testDir->cd("_"));
    Debug_printf("I'm in %s, changing to GAMES\n", testDir->url.c_str());
    // then LOAD"CD:GAMES",8
    testDir.reset(testDir->cd("GAMES"));
    Debug_printf("I'm in %s, changing to _\n", testDir->url.c_str());
    // then LOAD"CD_",8
    testDir.reset(testDir->cd("_"));
    Debug_printf("I'm in %s, changing to GAMES\n", testDir->url.c_str());
    // then LOAD"CD:GAMES",8
    testDir.reset(testDir->cd("GAMES"));
    Debug_printf("I'm in %s, changing to _\n", testDir->url.c_str());
    // then LOAD"CD_",8
    testDir.reset(testDir->cd("_"));
    Debug_printf("I'm in %s, changing to GAMES\n", testDir->url.c_str());
    // then LOAD"CD:GAMES",8
    testDir.reset(testDir->cd("GAMES"));
    Debug_printf("I'm in %s\n", testDir->url.c_str());
}


void readABit(Meat::mfilebuf<char>* pbuf)
{
    int i = 0;
    do {
        int nextChar = pbuf->sgetc(); // peeks next char BUT!!! donesn't move the buffer position
        if(nextChar != _MEAT_NO_DATA_AVAIL) {
            i++;
            pbuf->snextc(); // ok, there was real data in the buffer, let's actually ADVANCE buffer position
            Debug_printf("%c", nextChar); // or - send the char across IEC to our C64
        }
    } while (pbuf->sgetc() != EOF && i < 100);


}

void seekTest()
{
    Meat::iostream stream("https://www.w3.org/TR/PNG/iso_8859-1.txt");

    Debug_printv("Trying to open txt on http");

    if(!stream.is_open())
        return;

    // 1. we cen obtain raw C++ buffer from our stream:
    auto pbuf = stream.rdbuf();

    Debug_printv("Seeking");


    pbuf->seekpos(3542); // D7  MULTIPLICATION SIGN
    readABit(pbuf);
    pbuf->seekpos(3663); // D9  CAPITAL LETTER U WITH GRAVE
    readABit(pbuf);
    pbuf->seekpos(3598); // D8  CAPITAL LETTER O WITH STROKE
    readABit(pbuf);

    stream.close();
    
}


void commodoreServer()
{
    Meat::iostream stream("tcp://commodoreserver.com:1541");

    if(!stream.is_open())
        return;

    stream << "help\r\n";
    stream.sync();

    // 1. we cen obtain raw C++ buffer from our stream:
    auto pbuf = stream.rdbuf();

    do {
        int nextChar = pbuf->sgetc(); // peeks next char BUT!!! donesn't move the buffer position
        if(nextChar != _MEAT_NO_DATA_AVAIL) {
            pbuf->snextc(); // ok, there was real data in the buffer, let's actually ADVANCE buffer position
            Debug_printf("%c", nextChar); // or - send the char across IEC to our C64
        }
    } while (pbuf->sgetc() != EOF );

    stream.close();
    
}

void httpStream(char *url)
{
    size_t i = 0;
    size_t b_len = 1;
	uint8_t b[b_len];
    Debug_printv("Opening '%s'\r\n", url);
    std::unique_ptr<MFile> file(MFSOwner::File(url));

    if (file->exists())
    {
        size_t len = file->size();
        Debug_printv("File exists! size [%d]\r\n", len);

        Meat::iostream stream(url); // dstFile

        while(!stream.eof())
		{
			stream.read((char *)b, b_len);
        }
        stream.close();
        Debug_println("");
        Debug_printv("%d of %d bytes sent\r\n", i, len);
    }
    else
    {
        Debug_printv("File does not exist.\r\n");
    }
}

void httpGet(char *url)
{
    bool success = true;
    size_t i = 0;
    fnHttpClient http;
    size_t b_len = 1;
	uint8_t b[1];

    // http.setUserAgent("some agent");
    // http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    // http.setRedirectLimit(10);

    Debug_printf("[HTTP] begin... [%s]\n", url);
    if (http.begin( url )) {  // HTTP

      Serial.print("[HTTP] GET...\n");
      // start connection and send HTTP header
      int httpCode = http.GET();
      size_t len = http.available();

      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        Debug_printf("[HTTP] GET... code: %d\n", httpCode);

        // file found at server
        if (httpCode == HttpStatus_Ok || httpCode == HttpStatus_MovedPermanently) {

            for(i=0;i < len; i++)
            {
                success = http.read(b, b_len);
                if (success)
                {
                    Serial.write(b, b_len);
                }
            }
        }
      } else {
        Debug_printf("[HTTP] GET... failed, error: %d\n", httpCode);
      }
      http.close();
    } else {
      Debug_printf("[HTTP} Unable to connect\n");
    }
}

void testJson(MFile* srcFile, MFile* dstFile) {
    testHeader("C++ stream wrappers");

    StaticJsonDocument<512> m_device;
    deserializeJson(m_device, "{\"id\":0,\"media\":0,\"partition\":0,\"url\":\"http://niceurlman.com\",\"path\":\"/\",\"archive\":\"\",\"image\":\"\"}");

    if ( dstFile->exists() )
        dstFile->remove();

    Meat::iostream ostream(dstFile); // dstFile
    
    if(ostream.is_open()) {
        Debug_printf("Trying to serialize JSON to %s\n", dstFile->url.c_str());

        auto x = serializeJson(m_device, ostream); 

        Debug_printf("serializeJson returned %d\n", x);

        Debug_printf("sbefore if");

        if(ostream.bad()) {
            Debug_println("WARNING: FILE WRITE FAILED!!!");
        }

        Debug_printf("before close");

        ostream.close();
    }

    //Debug_printf("%s size is %d\n", dstFile->url.c_str(), dstFile->size());

    Debug_printf("Copy %s to %s\n", dstFile->url.c_str(), srcFile->url.c_str());

    bool copyRc = -1; //dstFile->copyTo(srcFile);

    Debug_printf("After copyto rc=%d\n", copyRc);

    if(copyRc) {
        Meat::iostream istream(srcFile);

        if(istream.is_open()) {
            Debug_printf("Trying to deserialize JSON from %s\n",srcFile->url.c_str());

            deserializeJson(m_device, istream);

            Debug_printf("Got from deserialization: %s\n", m_device["url"].as<const char*>());
        }
        else
        {
            Debug_printf("Error! The stream for deserialization couldn't be opened!");
        }
    }
    else {
        Debug_println("**** Copying failed *** WHY???");

        Debug_printf("Trying to deserialize JSON from %s\n",dstFile->url.c_str());

        Meat::iostream newIstream(dstFile); // this is your standard istream!

        if(newIstream.is_open()) {
            deserializeJson(m_device, newIstream);
        }

        Debug_printf("Got from deserialization: %s\n", m_device["url"].as<const char*>());

    }

}


void testReader(MFile* srcFile) {
    testHeader("TEST reading using C++ API");

    Debug_printf(" * Read test for %s\n", srcFile->url.c_str());

    Meat::iostream istream(srcFile);

    Debug_printv("reading file now!");
    if(istream.is_open()) {
        if(istream.eof()) {
            Debug_printf("Reader returned EOF! :(");
        }

        std::string line;

        while(!istream.eof()) {
            istream >> line;
            Serial.printf("%s", line.c_str());
        }

        istream.close();
    }
    else {
        Debug_printf(" * Read test - ERROR:%s could not be read!\n", srcFile->url.c_str());
    }

}

void testWriter(MFile* dstFile) {
    testHeader("TEST writing using C++ API");
    
    Debug_printf(" * Write test for %s\n", dstFile->url.c_str());

    Meat::iostream ostream(dstFile);

    Debug_println(" * Write test - after open\n");

    if ( dstFile->exists() )
        dstFile->remove();

    if(ostream.is_open()) {
        Debug_println(" * Write test - isOpen\n");

        ostream << "Arise, ye workers from your slumber,";
        ostream << "Arise, ye prisoners of want.";
        ostream << "For reason in revolt now thunders,";
        ostream << "and at last ends the age of cant!";
        if(ostream.bad())
            Debug_println("WRITING FAILED!!!");
            
        Debug_println(" * Write test - after testing bad\n");

        ostream.close();
        Debug_println(" * Write test - after close\n");
    }
    else {
        Debug_println(" * Write test - ERROR:The Internationale could not be written!\n");
    }
}

void runFSTest(std::string dirPath, std::string filePath) {
    testHeader("A full filesystem test");
    //Debug_println("**********************************************************************\n\n");

    auto testDir = Meat::New<MFile>(dirPath);
    auto testFile = Meat::New<MFile>(filePath);
    auto destFile = Meat::New<MFile>(testDir->cd("internationale.txt"));

    // if this doesn't work reading and writing files won't workk

    if(testFile != nullptr) {
        dumpFileProperties(testFile.get());
        testReader(testFile.get());
        //testWriter(destFile.get());
        //testReader(destFile.get());
    }
    else {
        Debug_printf("*** WARNING - %s instance couldn't be created!, , testDir->url.c_str()");
    }

    if(!dirPath.empty() && testDir->exists() && testDir->isDirectory()) {
        dumpFileProperties(testDir.get());
        testDirectory(testDir.get());
        //testPaths(testDir.get(),"subDir");
        //testRecursiveDir(otherFile.get(),""); // fucks upp littleFS?
    }
    else {
        Debug_printf("*** WARNING - %s instance couldn't be created!", testDir->url.c_str());
    }

    Debug_println("**********************************************************************\n\n");
}

void testSmartMFile() {
    testHeader("TEST smart MFile pointers");

    Debug_println("Creating smart MFile from char*");
    auto test = Meat::New<MFile>("cs://some/directory/disk.d64/file.txt");
    Debug_println("Creating smart MFile from MFile*");
    auto test2 = Meat::New<MFile>(test.get());

    auto wrapped = Meat::Wrap<MFile>(test2->getNextFileInDir());

	Debug_printf("Extension of second one: [%s]\n", test2->extension.c_str());
}

void testBasicConfig() {
    testHeader("TEST BASIC V2 config file");

    BasicConfigReader bcr;
    bcr.read("/config.prg");
    if(bcr.entries->size()>0) {
        Debug_printf("config Wifi SSID: [%s]\n", bcr.get("ssid"));
    }

}

void testRedirect() {
    testHeader("HTTP fs test");

    Meat::iostream istream("http://c64.meatloaf.cc/roms");

    if(istream.is_open()) {
        Debug_printf("* Stream OK!");


        if(istream.eof()) {
            Debug_printf("Reader returned EOF! :(");
        }

        Debug_printf("* read lines follow:\n");

        while(!istream.eof()) {
            std::string line;

            istream >> line;

            Debug_printf("LINE>%s\n",line.c_str());
        };
    }
    else {
        Debug_printf("* Couldn't open!");
    }

}

void testStrings() {
    testHeader("Testing strings");

    std::string s1("content-type");
    std::string s2("Content-Type");

    bool result = mstr::equals(s2, s1, false);

    Debug_printf("String-string case-insensitive comp:%d\n", result);

    result = mstr::equals(s2, "content-type", false);

    Debug_printf("String-char case-insensitive comp:%d\n", result);

    result = mstr::equals("Content-Type", "content-type", false);

    Debug_printf("char-char case-insensitive comp:%d\n", result);

    Debug_printf("pa == %s\n", mstr::drop("dupa",2).c_str());
    Debug_printf("du == %s\n", mstr::dropLast("dupa",2).c_str());

}

void runTestsSuite() {
    // Delay waiting for wifi to connect
    while ( !fnWiFi.connected() )
    {
        fnSystem.delay_microseconds(pdMS_TO_TICKS(1000)); // 1sec between checks
    }
    fnSystem.delay_microseconds(pdMS_TO_TICKS(5000)); // 5sec after connect

    //commodoreServer();
    seekTest();

    // ====== Per FS dir, read and write region =======================================

    // working, uncomment if you want
    //runFSTest("/.sys", "README"); // TODO - let urlparser drop the last slash!
    //runFSTest("http://c64.meatloaf.cc/roms", "https://www.w3.org/TR/PNG/iso_8859-1.txt");
    // http://c64.meatloaf.cc/roms
    //runFSTest("http://192.168.1.161:8000", "https://www104.zippyshare.com/d/TEh31GeR/1191019/GeckOS-c64.d64/index.html");
    //runFSTest("https://c64.meatloaf.cc/geckos-c64.d64", "https://c64.meatloaf.cc/geckos-c64.d64/index.html");
    //runFSTest("sd:/geckos-c64.d64", "sd:/geckos-c64.d64/index.html");
    //  https://c64.meatloaf.cc
    // runFSTest("http://info.cern.ch/hypertext/WWW/TheProject.html","http://info.cern.ch/hypertext/WWW/TheProject.html");
    // runFSTest("cs:/apps/ski_writer.d64","cs:/apps/ski_writer.d64/EDITOR.HLP");


    // ====== Misc test region =======================================
        
    //testIsDirectory();
    //testUrlParser();
    //testCD();

    //htmlStream("HTTP://MEATLOAF.CC");  // Doesn't work
    //htmlStream("http://MEATLOAF.CC");  // Works!!!
    //htmlStream("http://meatloaf.cc");  // Works!!!

    //testDiscoverDevices();
    // testCDMFile("CCGMS", 0);
    // testCDMFile("CD:GAMES", 0);
    // testCDMFile("CD_", 0);
    // testCDMFile("CD/GAMES", 0);
    // testCDMFile("CD_", 0);
    // testCDMFile("CDGAMES", 0);
    // testCDMFile("CD_", 0);
    // testCDMFile("CDGAMES", 15);
    // testCDMFile("CD_", 0);

    // D64 Test
    // Debug_printv("D64 Test");
    // testDirectory(MFSOwner::File("/games/arcade7.d64"), true);
    // testBasicConfig();

    // Debug_printv("Flash File System");
    //testDirectoryStandard("/");

    // Debug_printv("SD Card File System");
    //std::string basepath = fnSDFAT.basepath();
    //basepath += std::string("/");
    //Debug_printv("basepath[%s]", basepath.c_str());
    //testDirectory(MFSOwner::File( basepath ), true);
    //testDirectoryStandard( "/sd/" );
    // testDirectory(MFSOwner::File("/sd/"), true);

    // DeviceDB m_device(0);

    // Debug_println(m_device.path().c_str());
    // m_device.select(8);
    // Debug_println(m_device.path().c_str());
    // m_device.select(9);
    // Debug_println(m_device.path().c_str());
    // m_device.select(10);
    // Debug_println(m_device.path().c_str());
    // m_device.select(11);
    // Debug_println(m_device.path().c_str());
    // m_device.select(30);
    // Debug_println(m_device.path().c_str());

    //testRedirect();
    //testStrings();

    Debug_println("*** All tests finished ***");
}
