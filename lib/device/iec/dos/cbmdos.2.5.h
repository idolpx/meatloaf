
#include "_dos.h"

#include <string>

class CBMDOS_2_5: DOS
{
    public:
        void parse_command(std::string command) override;
};