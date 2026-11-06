// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The floors' file-operation input, independent of the host executable's linkage and ISA. Prefer its mapped libc;
// static executables have no such mapping, so search the linker's installed runtime directories. An explicit path
// is authoritative. This is untimed discovery and an ELF envelope check, not a load or execution of the input.

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "buster/compiler/runtime_paths.h"

#define BUSTER_FLOORS_INPUT_PATH_BYTES 4096

typedef struct FloorsInput FloorsInput;
struct FloorsInput
{
    char path[BUSTER_FLOORS_INPUT_PATH_BYTES];
    const char* source;
    struct stat status;
    int file;
};

// Returns an errno value, preserving failures across close. On success the caller owns input->file; validation and
// the later pread/mmap samples use that same descriptor, so the reported identity is the file actually sampled.
static int floors_input_open(FloorsInput* input, const char* path, const char* source, uint64_t maximum_size)
{
    const size_t length = strlen(path);
    int error = length < sizeof(input->path) ? 0 : ENAMETOOLONG;
    const int file = error == 0 ? open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK) : -1;
    if (error == 0 && file < 0)
    {
        error = errno;
    }
    struct stat status;
    if (error == 0 && fstat(file, &status) != 0)
    {
        error = errno;
    }
    if (error == 0)
    {
        error = !S_ISREG(status.st_mode) ? EINVAL : status.st_size < (off_t)sizeof(Elf64_Ehdr) ? ENOEXEC :
                (uint64_t)status.st_size > maximum_size ? EFBIG : 0;
    }
    Elf64_Ehdr header;
    size_t read_bytes = 0;
    while (error == 0 && read_bytes < sizeof(header))
    {
        const ssize_t count = pread(file, (char*)&header + read_bytes, sizeof(header) - read_bytes, (off_t)read_bytes);
        if (count > 0)
        {
            read_bytes += (size_t)count;
        }
        else if (count == 0 || errno != EINTR)
        {
            error = count == 0 ? ENOEXEC : errno;
        }
    }
    if (error == 0)
    {
        const bool valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_ident[EI_CLASS] == ELFCLASS64 &&
                           header.e_ident[EI_DATA] == ELFDATA2LSB && header.e_ident[EI_VERSION] == EV_CURRENT &&
                           header.e_type == ET_DYN && header.e_machine == EM_X86_64 && header.e_version == EV_CURRENT &&
                           header.e_ehsize == sizeof(header);
        error = valid ? 0 : ENOEXEC;
    }
    if (error == 0)
    {
        memcpy(input->path, path, length + 1);
        input->source = source;
        input->status = status;
        input->file = file;
    }
    else if (file >= 0)
    {
        close(file);
    }
    return error;
}

// The stream and directories are explicit so tests can model a static host, a mapped libc, and an absent runtime
// without modifying the system. A failed explicit path never falls through to an unrelated installed library.
static int floors_input_select(FloorsInput* input, const char* requested, FILE* maps, const char* const* directories, size_t directory_count, uint64_t maximum_size)
{
    *input = (FloorsInput){ .file = -1 };
    int error = ENOENT;
    if (requested != NULL)
    {
        error = floors_input_open(input, requested, "--libc", maximum_size);
    }
    else
    {
        char line[BUSTER_FLOORS_INPUT_PATH_BYTES + 512];
        while (input->file < 0 && maps != NULL && fgets(line, sizeof(line), maps) != NULL)
        {
            char* newline = strchr(line, '\n');
            if (newline == NULL)
            {
                // Never interpret the tail of an overlong mapping line as a fresh mapping.
                int byte = 0;
                while (byte != '\n' && byte != EOF)
                {
                    byte = fgetc(maps);
                }
                continue;
            }
            const char* name = strchr(line, '/');
            const size_t length = name != NULL ? (size_t)(newline - name) : 0;
            const size_t suffix = sizeof("/libc.so.6") - 1;
            if (length >= suffix && memcmp(name + length - suffix, "/libc.so.6", suffix) == 0)
            {
                *newline = 0;
                const int attempt = floors_input_open(input, name, "/proc/self/maps", maximum_size);
                error = attempt != ENOENT && attempt != ENOTDIR ? attempt : error;
            }
        }
        for (size_t d = 0; input->file < 0 && d < directory_count; d += 1)
        {
            char path[BUSTER_FLOORS_INPUT_PATH_BYTES];
            const int length = snprintf(path, sizeof(path), "%s/libc.so.6", directories[d]);
            const int attempt = length < 0 || (size_t)length >= sizeof(path) ? ENAMETOOLONG :
                                floors_input_open(input, path, "runtime library directories", maximum_size);
            error = attempt != ENOENT && attempt != ENOTDIR ? attempt : error;
        }
    }
    return input->file >= 0 ? 0 : error;
}

static int floors_input_find(FloorsInput* input, const char* requested, uint64_t maximum_size)
{
    FILE* maps = requested == NULL ? fopen("/proc/self/maps", "r") : NULL;
    const int error = floors_input_select(input, requested, maps, runtime_library_directories,
                                         sizeof(runtime_library_directories) / sizeof(runtime_library_directories[0]), maximum_size);
    if (maps != NULL)
    {
        fclose(maps);
    }
    return error;
}
