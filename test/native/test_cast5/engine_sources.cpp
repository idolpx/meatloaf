// The cipher under test. It lives under components/ rather than lib/, so it is
// #include'd here for the same reason the other native suites include their
// engine sources: PlatformIO's LDF does not reach outside lib/ for this env.
//
// cast5.c is C89 plus GNU statement expressions; g++ accepts both.
#include "../../../components/afpfs-ng/esp32/cast5.c"
