#pragma once
#include <buster/file.h>

BUSTER_IMPL bool file_write(StringOs path, ByteSlice content)
{
    let fd = os_file_open(path, (OpenFlags) { .write = 1, .create = 1, .truncate = 1 }, (OpenPermissions){ .read = 1, .write = 1 });
    bool result = {};

    result = fd != 0;
    if (result)
    {
        os_file_write(fd, content);
        os_file_close(fd);
    }

    return result;
}

