// dropcache: evict the page-cache pages of every file named on stdin (one path
// per line) with posix_fadvise(POSIX_FADV_DONTNEED), after fsync so dirty pages
// do not pin them. Unprivileged: evicts only clean pages of files the caller
// can open, which is what a cold-start experiment on immutable inputs needs.
// Prints how many files were processed and how many bytes they hold.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int main(void)
{
    char line[4096];
    unsigned long files = 0;
    unsigned long long bytes = 0;
    unsigned long failed = 0;
    while (fgets(line, sizeof(line), stdin))
    {
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = 0;
        if (!length) continue;
        int fd = open(line, O_RDONLY);
        if (fd < 0)
        {
            failed += 1;
            continue;
        }
        struct stat st;
        if (fstat(fd, &st) == 0)
        {
            bytes += (unsigned long long)st.st_size;
        }
        fdatasync(fd);
        if (posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) != 0) failed += 1;
        close(fd);
        files += 1;
    }
    printf("DROPCACHE files=%lu bytes=%llu failed=%lu\n", files, bytes, failed);
    return 0;
}
