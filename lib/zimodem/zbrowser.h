/*
   Copyright 2016-2019 Bo Zimmerman

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifdef INCLUDE_SD_SHELL
class ZBrowser : public ZMode
{
  private:
    enum ZBrowseState
    {
      ZBROW_MAIN=0,
    } currState;
    
    ZSerial serial;

    void switchBackToCommandMode();
    std::string makePath(std::string addendum);
    std::string fixPathNoSlash(std::string path);
    std::string stripDir(std::string path);
    std::string stripFilename(std::string path);
    std::string stripArgs(std::string line, std::string &argLetters);
    std::string cleanOneArg(std::string line);
    std::string cleanFirstArg(std::string line);
    std::string cleanRemainArg(std::string line);
    bool isMask(std::string mask);
    bool matches(std::string fname, std::string mask);
    void makeFileList(std::string ***l, int *n, std::string p, std::string mask, bool recurse);
    void deleteFile(std::string fname, std::string mask, bool recurse);
    void showDirectory(std::string path, std::string mask, std::string prefix, bool recurse);
    void copyFiles(std::string source, std::string mask, std::string target, bool recurse, bool overwrite);
    
    FTPHost *ftpHost = 0;
    bool showMenu;
    bool savedEcho;
    std::string path="/";
    std::string EOLN;
    char EOLNC[5];
    unsigned long lastNumber;
    std::string lastString;

  public:
    ~ZBrowser();
    void switchTo();
    void serialIncoming();
    void loop();
    void init();
    void doModeCommand(std::string &line);
};
#endif
