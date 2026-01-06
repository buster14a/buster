#pragma once
#include <buster/file.h>
#include <buster/integer.h>
#include <buster/arena.h>

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

BUSTER_IMPL ByteSlice file_read(Arena* arena, StringOs path, FileReadOptions options)
{
    let fd = os_file_open(path, (OpenFlags) { .read = 1 }, (OpenPermissions){ .read = 1 });
    ByteSlice result = {};

    if (fd)
    {
        if (!options.start_alignment)
        {
            options.start_alignment = 1;
        }

        if (!options.end_alignment)
        {
            options.end_alignment = 1;
        }

        let file_size = os_file_get_size(fd);
        let file_buffer = (u8*)&file_read;
        if (file_size)
        {
            let allocation_size = align_forward(file_size + options.start_padding + options.end_padding, options.end_alignment);
            let allocation_bottom = allocation_size - (file_size + options.start_padding);
            let allocation_alignment = BUSTER_MAX(options.start_alignment, 1);
            file_buffer = (u8*)arena_allocate_bytes(arena, allocation_size, allocation_alignment);
            file_size = os_file_read(fd, (ByteSlice) { file_buffer + options.start_padding, file_size }, file_size);
            memset(file_buffer + options.start_padding + file_size, 0, allocation_bottom);
        }
        os_file_close(fd);
        result = (ByteSlice) { file_buffer + options.start_padding, file_size };
    }

    return result;
}

