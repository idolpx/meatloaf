/*
 * This is free and unencumbered software released into the public domain.
 *
 * For more information, please refer to <https://unlicense.org>
 *
 * https://gist.github.com/RabaDabaDoba/145049536f815903c79944599c6f952a
 *
 */

/*
    Color Codes, Escapes & Languages

    I was able to use colors in my terminal by using a variety of different escape values. 
    As the conversation implies above, different languages require different escapes, 
    furthermore; there are several different sequences that are implemented for ANSI escapes, 
    and they can vary quite a bit. Some escapes have a cleaner sequence than others. 
    Personally I like the \e way of writing an escape, as it is clean and simple. 
    However, I couldn't get it to work anywhere, save the BASH scripting language.

    Each escape works with its adjacent language

        \x1b  👉‍    Node.js
        \x1b  👉‍    Node.js w/ TS
        \033  👉‍    GNU Cpp
        \033  👉‍    ANSI C
        \e    👉‍    BASH
*/

//Regular text
#define BLK "\e[0;30m"
#define RED "\e[0;31m"
#define GRN "\e[0;32m"
#define YEL "\e[0;33m"
#define BLU "\e[0;34m"
#define MAG "\e[0;35m"
#define CYN "\e[0;36m"
#define WHT "\e[0;37m"

//Regular bold text
#define BBLK "\e[1;30m"
#define BRED "\e[1;31m"
#define BGRN "\e[1;32m"
#define BYEL "\e[1;33m"
#define BBLU "\e[1;34m"
#define BMAG "\e[1;35m"
#define BCYN "\e[1;36m"
#define BWHT "\e[1;37m"

//Regular dim text
#define DBLK "\e[2;30m"
#define DRED "\e[2;31m"
#define DGRN "\e[2;32m"
#define DYEL "\e[2;33m"
#define DBLU "\e[2;34m"
#define DMAG "\e[2;35m"
#define DCYN "\e[2;36m"
#define DWHT "\e[2;37m"

//Regular italics text
#define IBLK "\e[3;30m"
#define IRED "\e[3;31m"
#define IGRN "\e[3;32m"
#define IYEL "\e[3;33m"
#define IBLU "\e[3;34m"
#define IMAG "\e[3;35m"
#define ICYN "\e[3;36m"
#define IWHT "\e[3;37m"

//Regular underline text
#define UBLK "\e[4;30m"
#define URED "\e[4;31m"
#define UGRN "\e[4;32m"
#define UYEL "\e[4;33m"
#define UBLU "\e[4;34m"
#define UMAG "\e[4;35m"
#define UCYN "\e[4;36m"
#define UWHT "\e[4;37m"

//Regular reversed text
#define RVBLK "\e[7;30m"
#define RVRED "\e[7;31m"
#define RVGRN "\e[7;32m"
#define RVYEL "\e[7;33m"
#define RVBLU "\e[7;34m"
#define RVMAG "\e[7;35m"
#define RVCYN "\e[7;36m"
#define RVWHT "\e[7;37m"

//Regular background
#define BLKB "\e[40m"
#define REDB "\e[41m"
#define GRNB "\e[42m"
#define YELB "\e[43m"
#define BLUB "\e[44m"
#define MAGB "\e[45m"
#define CYNB "\e[46m"
#define WHTB "\e[47m"

//High intensty background 
#define BLKHB "\e[0;100m"
#define REDHB "\e[0;101m"
#define GRNHB "\e[0;102m"
#define YELHB "\e[0;103m"
#define BLUHB "\e[0;104m"
#define MAGHB "\e[0;105m"
#define CYNHB "\e[0;106m"
#define WHTHB "\e[0;107m"

//High intensty text
#define HBLK "\e[0;90m"
#define HRED "\e[0;91m"
#define HGRN "\e[0;92m"
#define HYEL "\e[0;93m"
#define HBLU "\e[0;94m"
#define HMAG "\e[0;95m"
#define HCYN "\e[0;96m"
#define HWHT "\e[0;97m"

//Bold high intensity text
#define BHBLK "\e[1;90m"
#define BHRED "\e[1;91m"
#define BHGRN "\e[1;92m"
#define BHYEL "\e[1;93m"
#define BHBLU "\e[1;94m"
#define BHMAG "\e[1;95m"
#define BHCYN "\e[1;96m"
#define BHWHT "\e[1;97m"

//Reset
#define RESET "\e[0m"
#define CRESET "\e[0m"
#define COLOR_RESET "\e[0m"
