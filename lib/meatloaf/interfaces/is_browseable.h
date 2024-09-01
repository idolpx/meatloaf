
#include <string>

class IsBrowseable
{
public:
    // For files with no directory structure
    // tap, crt, tar
     virtual std::string seekNextEntry()= 0;
};