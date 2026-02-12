// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#ifndef MEATLOAF_PUP_H
#define MEATLOAF_PUP_H

#include <cstdint>
#include <memory>
#include <string>

class PeoplesUrlParser
{
private:
    void processHostPort(std::string hostPort);
    void processAuthorityPath(std::string authorityPath);
    void processUserPass(std::string userPass);
    void processAuthority(std::string pastTheColon);
    void cleanPath();
    void processPath();

protected:
    PeoplesUrlParser() {};

public:
    ~PeoplesUrlParser() {};

    std::string mRawUrl;
    std::string url;
    std::string scheme;
    std::string user;
    std::string password;    
    std::string host;
    std::string port;
    std::string path;
    std::string name;
    std::string base_name;
    std::string extension;
    std::string query;
    std::string fragment;

    std::string pathToFile(void);
    std::string root(void);
    std::string base(void);

    uint16_t getPort();

    static std::unique_ptr<PeoplesUrlParser> parseURL(const std::string &u);
    void resetURL(const std::string u);
    std::string rebuildUrl(void);
    bool isValidUrl();

    std::string queryValue(const std::string& key, bool caseInsensitive = true) const;
    std::string fragmentValue(const std::string& key, bool caseInsensitive = true) const;

    void dump();

};

#endif // MEATLOAF_PUP_H