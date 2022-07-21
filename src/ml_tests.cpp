

#include <string>

#include <dirent.h>
#include <sys/stat.h>
#include <ArduinoJson.h>

#include "../include/global_defines.h"
#include "../include/debug.h"

#include "fnFsSd.h"

#include "ml_tests.h"
#include "meat_io.h"
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

void testReader(MFile* readeTest) {
    // /* Test Line reader */
    testHeader("C++ line reader");

    Debug_printf("* Trying to read file:%s\n", readeTest->url.c_str());

    auto readerStream = Meat::ifstream(readeTest);
    readerStream.open();

    if(readerStream.is_open()) {
        if(readerStream.eof()) {
            Debug_printf("Reader returned EOF! :(");
        }

        while(!readerStream.eof()) {
            std::string line;

            readerStream >> line;

            Debug_printf("%s\n",line.c_str());
        };
    }
    else {
        Debug_printf("*** ERROR: stream could not be opened!");
    }
}

void dumpFileProperties(MFile* testMFile) {
    Debug_println("\n== File properties ==");
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
    Debug_printf("-------------------------------\n");
}

void testDirectory(MFile* dir, bool verbose=false) {
    testHeader("A directory");

    Debug_printf("* Listing %s\n", dir->url.c_str());
    std::unique_ptr<MFile> entry(dir->getNextFileInDir());

    while(entry != nullptr) {
        if(verbose)
            dumpFileProperties(entry.get());
        else
            Debug_printf("'%s'\n", entry->url.c_str());
        entry.reset(dir->getNextFileInDir());
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
    testHeader("Copy file to destination");

    Debug_printf("FROM:%s\nTO:%s\n", srcFile->url.c_str(), dstFile->url.c_str());

    if(dstFile->exists()) {
        bool result = dstFile->remove();
        Debug_printf("FSTEST: %s existed, delete reult: %d\n", dstFile->path.c_str(), result);
    }

    srcFile->copyTo(dstFile);
}

void dumpParts(std::vector<std::string> v) {
    for(auto i = v.begin(); i < v.end(); i++)
        Debug_printf("%s::",(*i).c_str());
}

void testStringFunctions() {
    testHeader("String functions");
    Debug_printf("pa == %s\n", mstr::drop("dupa",2).c_str());
    Debug_printf("du == %s\n", mstr::dropLast("dupa",2).c_str());
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

void httpStream(char *url)
{
    bool success = true;
    size_t i = 0;
    size_t b_len = 1;
	uint8_t b[b_len];
    Debug_printv("Opening '%s'\r\n", url);
    std::unique_ptr<MFile> file(MFSOwner::File(url));

    if (file->exists())
    {
        size_t len = file->size();
        Debug_printv("File exists! size [%d]\r\n", len);

        std::unique_ptr<MIStream> stream(file->inputStream());

		for(i=0;i < len; i++)
		{
			success = stream->read(b, b_len);
			if (success)
			{
                Serial.write(b, b_len);
            }
        }
        stream->close();
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

void testStdStreamWrapper(MFile* srcFile, MFile* dstFile) {
    testHeader("C++ stream wrappers");

    StaticJsonDocument<512> m_device;
    deserializeJson(m_device, "{\"id\":0,\"media\":0,\"partition\":0,\"url\":\"http://niceurlman.com\",\"path\":\"/\",\"archive\":\"\",\"image\":\"\"}");

    if ( dstFile->exists() )
        dstFile->remove();

    Meat::ofstream ostream(dstFile); // dstFile
    ostream.open();
    
    if(ostream.is_open()) {
        Debug_printf("Trying to serialize JSON to %s\n", dstFile->url.c_str());

        auto x = serializeJson(m_device, ostream); 

        Debug_printf("serializeJson returned %d\n", x);

        if(ostream.bad()) {
            Debug_println("WARNING: FILE WRITE FAILED!!!");
        }

        ostream.close();
    }

    //Debug_printf("%s size is %d\n", dstFile->url.c_str(), dstFile->size());

    Debug_printf("Copy %s to %s\n", dstFile->url.c_str(), srcFile->url.c_str());

    if(dstFile->copyTo(srcFile)) {
        Meat::ifstream istream(srcFile);
        istream.open();

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

        Meat::ifstream newIstream(dstFile); // this is your standard istream!
        newIstream.open();

        deserializeJson(m_device, newIstream);

        Debug_printf("Got from deserialization: %s\n", m_device["url"].as<const char*>());

    }

}


void testNewCppStreams(std::string name) {
    testHeader("TEST C++ streams");

    Debug_println(" * Read test\n");

    Meat::ifstream istream(name);
    istream.open();
    if(istream.is_open()) {
        std::string line;

        while(!istream.eof()) {
            istream >> line;
            Serial.print(line.c_str());
        }

        istream.close();
    }

    Debug_println("\n * Write test\n");

    Meat::ofstream ostream("/intern.txt");

    Debug_println(" * Write test - after declaration\n");

    ostream.open();
    if(ostream.is_open()) {
        ostream << "Arise, ye workers from your slumber,";
        ostream << "Arise, ye prisoners of want.";
        ostream << "For reason in revolt now thunders,";
        ostream << "and at last ends the age of cant!";
        if(ostream.bad())
            Debug_println("WRITING FAILED!!!");

        ostream.close();
    }
}

void runFSTest(std::string dirPath, std::string filePath) {
    //Debug_println("**********************************************************************\n\n");
    // std::shared_ptr<MFile> testDir(MFSOwner::File(dirPath));
    // std::shared_ptr<MFile> testFile(MFSOwner::File(filePath));
    // std::shared_ptr<MFile> destFile(MFSOwner::File("/mltestfile"));

    auto testDir = Meat::New<MFile>(dirPath);
    auto testFile = Meat::New<MFile>(filePath);
    auto destFile = Meat::New<MFile>("/mltestfile");

    testNewCppStreams(filePath);

    if(!dirPath.empty() && testDir != nullptr) {
        testPaths(testDir.get(),"subDir");
        testDirectory(testDir.get());
        //testRecursiveDir(otherFile.get(),""); // fucks upp littleFS?
    }
    else {
        Debug_println("*** WARNING - directory instance couldn't be created!");
    }

    if(!filePath.empty() && testFile != nullptr) {
        testReader(testFile.get());
        testCopy(testFile.get(), destFile.get());

        testStdStreamWrapper(testFile.get(), destFile.get());

        Debug_println("\n\n\n*** Please compare file copied to ML aginst the source:\n\n\n");
        testReader(destFile.get());
    }
    else {
        Debug_println("*** WARNING - file instance couldn't be created!");
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

void runTestsSuite() {
    // working, uncomment if you want
    // runFSTest("/.sys", "README"); // TODO - let urlparser drop the last slash!
    // runFSTest("http://google.com/we/love/commodore/disk.d64/somefile","http://jigsaw.w3.org/HTTP/connection.html");
    //runFSTest("cs:/apps/ski_writer.d64","cs:/apps/ski_writer.d64/EDITOR.HLP");
    
    // not working yet, DO NOT UNCOMMENT!!!
    //runFSTest("http://somneserver.com/utilities/disk tools/cie.dnp/subdir/CIE+SERIAL","");    
    
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

    Debug_printv("Flash File System");
    //testDirectory(MFSOwner::File("/"), true);
    testDirectoryStandard("/");

    Debug_printv("SD Card File System");
    //std::string basepath = fnSDFAT.basepath();
    //basepath += std::string("/");
    //Debug_printv("basepath[%s]", basepath.c_str());
    //testDirectory(MFSOwner::File( basepath ), true);
    testDirectoryStandard( "/sd/" );


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


    Debug_println("*** All tests finished ***");
}
