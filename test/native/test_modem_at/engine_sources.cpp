// Pulls in the exact translation units the modem AT tests need, by
// #include-ing the real .cpp files by relative path. Same approach as
// test/native/test_console_dos/engine_sources.cpp -- see that file for why
// PlatformIO's library dependency finder cannot be used here.
//
// Everything included below is deliberately free of ESP-IDF, MStream and
// lib/console dependencies. If a future edit makes one of these files pull in
// FreeRTOS or MFSOwner, this suite stops building -- which is the point.

// ENABLE_MODEM gates the firmware build. The native suite always wants these
// units, so define it here rather than in the native env's build_flags, which
// would also switch on code paths that need ESP-IDF.
#define ENABLE_MODEM 1

#include "../../../lib/modem/at_parser.cpp"
