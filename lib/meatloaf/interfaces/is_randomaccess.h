
#include <string>

class IsRandomAccess
{
public:
    // For files with a browsable random access directory structure
    // d64, d74, d81, dnp, etc.
     virtual bool seekPath(std::string path)= 0;
};