/*
 * ESP32 compatibility stubs for libiscsi.
 *
 * dup2() is used only in the reconnection path (iscsi->old_iscsi != NULL),
 * which is never exercised by our synchronous single-shot iSCSI connections.
 * The linker still needs the symbol, so we provide a stub that returns -1.
 */

#include <errno.h>

int dup2(int oldfd, int newfd)
{
    (void)oldfd;
    (void)newfd;
    errno = ENOSYS;
    return -1;
}
