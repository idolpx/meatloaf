
#include <string>
#include <unordered_map>

class HasInfo
{
public:
    virtual std::unordered_map<std::string, std::string> info() = 0;

};