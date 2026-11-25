#ifndef ML_STUB_SYS_WAIT_H
#define ML_STUB_SYS_WAIT_H
#ifndef _WIN32
/* A POSIX host (Linux, macOS) has the real header, with waitpid(). */
#include_next <sys/wait.h>
#else
/* mingw has no sys/wait.h; libarchive's archive_windows.h supplies waitpid. */
#ifndef WIFEXITED
#define WIFEXITED(s) (1)
#define WEXITSTATUS(s) (s)
#define WIFSIGNALED(s) (0)
#define WTERMSIG(s) (0)
#endif
#endif
#endif
