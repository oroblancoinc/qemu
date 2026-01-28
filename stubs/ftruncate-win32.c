/*
 * Stub for qemu_ftruncate64 used by tests on Windows
 *
 * The real implementation is in block/file-win32.c, but tests that
 * use ftruncate don't link against the block library.
 */

#include "qemu/osdep.h"

#ifdef _WIN32
#include <io.h>

int qemu_ftruncate64(int fd, int64_t length)
{
    HANDLE hFile = (HANDLE)_get_osfhandle(fd);
    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }

    LARGE_INTEGER li;
    li.QuadPart = length;
    if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN)) {
        return -1;
    }
    if (!SetEndOfFile(hFile)) {
        return -1;
    }
    return 0;
}
#endif
