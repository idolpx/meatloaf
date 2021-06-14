#ifndef _FN_STRUTILS_H
#define _FN_STRUTILS_H

#include <string>
#include <vector>
#include <cstring>

namespace mstr {
    std::string drop(std::string str, int count);
    std::string dropLast(std::string str, int count);
    bool startsWith(std::string s, const char *pattern, bool case_sensitive = true);
    bool endsWith(std::string s, const char *pattern, bool case_sensitive = true);
    bool equals(std::string &s1, std::string &s2, bool case_sensitive = true);
    std::vector<std::string> split(std::string toSplit, char ch, int limit = 9999);
    void toLower(std::string &s);
    void toUpper(std::string &s);
}

#endif