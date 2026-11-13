// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Choose the archive-capable route before compiling a C-only source command. DSO-only commands keep the direct
// importer's version checks and runtime snapshot (#1470); this probe reads no source or archive member.
static bool check_driver_library_archive(const char* path, bool named_archive)
{
    const int file = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    struct stat status;
    char header[sizeof(BUSTER_IMPORT_ARCHIVE_MAGIC) - 1];
    uint32_t used = 0;
    const bool regular = file >= 0 && fstat(file, &status) == 0 && S_ISREG(status.st_mode) && status.st_size > 0;
    while (regular && used < sizeof(header))
    {
        const ssize_t count = read(file, header + used, sizeof(header) - used);
        if (count > 0)
        {
            used += (uint32_t)count;
        }
        else if (count == 0 || errno != EINTR)
        {
            break;
        }
    }
    if (file >= 0)
    {
        close(file);
    }
    // Empty archives add nothing and already work on the direct path. A selected .a can also be a GNU ld script
    // naming archives (glibc's libm.a), which the foreign reader expands. Probe magic as well as the search's suffix:
    // -l:FILE can name an archive under any filename. A failed read stays with the ordinary importer's diagnostic.
    const bool read = regular && (used == sizeof(header) || used == (uint64_t)status.st_size);
    const bool magic = used == sizeof(header) && memcmp(header, BUSTER_IMPORT_ARCHIVE_MAGIC, sizeof(header)) == 0;
    const bool empty = magic && status.st_size == (off_t)sizeof(header);
    return read && !empty && (named_archive || magic ||
                             (used == sizeof(header) && memcmp(header, BUSTER_IMPORT_THIN_ARCHIVE_MAGIC, sizeof(header)) == 0));
}

static bool check_driver_named_archives(CheckDriverLibraries* libraries, RuntimeKind kind, Arena* arena, bool* mixed)
{
    Runtime runtime = { 0 };
    const RuntimeError discovered = runtime_discover_inputs(&runtime, kind);
    bool ready = discovered == RUNTIME_ERROR_NONE;
    if (!ready)
    {
        fprintf(stderr, "error: can't find the C runtime inputs%s%s\n", runtime.failed_file[0] != 0 ? ": " : "", runtime.failed_file);
    }
    for (uint32_t i = 0; ready && i < libraries->argument_count; i += 1)
    {
        ForeignArgument* argument = &libraries->arguments[i];
        if ((argument->flags & BUSTER_FOREIGN_INPUT_LIBRARY) != 0)
        {
            const bool exact = argument->name.length > 1 && argument->name.pointer[0] == ':';
            const String8 name = { .pointer = argument->name.pointer + exact, .length = argument->name.length - exact };
            const bool static_only = (argument->flags & BUSTER_FOREIGN_INPUT_STATIC) != 0;
            char path[BUSTER_RUNTIME_PATH_BYTES];
            bool archive = false;
            const bool found = runtime_libraries_search(&runtime, name, exact, static_only, libraries->directories,
                                                        libraries->directory_count, path, &archive);
            if (found && check_driver_library_archive(path, archive))
            {
                const uint64_t length = strlen(path);
                char* copy = arena_push(arena, length + 1, 1);
                ready = copy != NULL;
                if (ready)
                {
                    memcpy(copy, path, length + 1);
                    argument->name = (String8){ .pointer = copy, .length = length };
                    argument->flags &= (uint8_t)~BUSTER_FOREIGN_INPUT_LIBRARY;
                    *mixed = true;
                }
            }
        }
    }
    runtime_release(&runtime);
    return ready;
}
