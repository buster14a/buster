// Temporary witness for the optional cold-writer page advice.
#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

int madvise(void* address, size_t bytes, int advice)
{
    if (advice == 23)
    {
        static const char message[] = "declined writable snapshot advice\n";
        if (write(STDERR_FILENO, message, sizeof(message) - 1) != (ssize_t)(sizeof(message) - 1))
        {
            _exit(125);
        }
        errno = EINVAL;
        return -1;
    }
    return (int)syscall(SYS_madvise, address, bytes, advice);
}
