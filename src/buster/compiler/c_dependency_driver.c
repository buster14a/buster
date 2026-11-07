// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Driver ownership around c_dependency.c: option state, Clang-style filenames, complete rule buffers and atomic
// publication. Each unit makes its buffer in caller-owned memory. The command publishes buffers in argv order,
// after its units succeed; neither parallel units nor diagnostic-history replays write dependency files.

#include "string8.h"
#include "c_dependency.c"

typedef struct DependencyOptions DependencyOptions;
struct DependencyOptions
{
    bool enabled;
    bool only;
    bool include_system;
    bool phony;
    const char* path;
    DependencyTarget* targets;
    uint32_t target_count;
};

typedef struct DependencyOutput DependencyOutput;
struct DependencyOutput
{
    const char* path;
    String8 bytes;
};

// Clang removes leading ./ components from prerequisites, but leaves interior components and target spellings alone.
static String8 dependency_prerequisite(const char* path)
{
    while (path[0] == '.' && path[1] == '/')
    {
        path += 2;
    }
    return (String8){ .pointer = (char*)path, .length = strlen(path) };
}

// Replace a suffix on the last path component, keeping an explicit output's directory. With no -o, objects and
// depfiles go in the current directory, not beside the source. A leading dot alone is part of the basename.
static const char* dependency_filename(Arena* arena, const char* source, const char* output, char suffix)
{
    const char* name = output;
    if (name == NULL)
    {
        const char* slash = strrchr(source, '/');
        name = slash != NULL ? slash + 1 : source;
    }
    const char* slash = strrchr(name, '/');
    const char* base = slash != NULL ? slash + 1 : name;
    const char* dot = strrchr(base, '.');
    const uint64_t length = dot != NULL && dot != base ? (uint64_t)(dot - name) : strlen(name);
    char* result = length <= UINT64_MAX - 3 ? arena_push(arena, length + 3, 1) : NULL;
    if (result != NULL)
    {
        memcpy(result, name, length);
        result[length] = '.';
        result[length + 1] = suffix;
        result[length + 2] = 0;
    }
    return result;
}

static const char* dependency_destination(const DependencyOptions* options, const char* source, const char* output, Arena* arena)
{
    return options->path != NULL ? options->path : options->only ? (output != NULL ? output : "-") : dependency_filename(arena, source, output, 'd');
}

// Preparation owns no filesystem resources. The output is either a complete rule in arena or remains empty.
static bool dependency_prepare(DependencyOutput* output, const DependencyOptions* options, const DependencyFile* files, uint32_t file_count, const char* source,
                               const char* primary, bool scalar, Arena* arena, FILE* errors)
{
    *output = (DependencyOutput){ 0 };
    bool result = true;
    if (options->enabled)
    {
        const bool input_stdin = strcmp(source, "-") == 0;
        const char* target = !options->only && primary != NULL ? primary : dependency_filename(arena, source, NULL, 'o');
        const char* path = dependency_destination(options, source, primary, arena);
        const DependencyRule rule = {
            .source = input_stdin ? (String8){ 0 } : dependency_prerequisite(source),
            .object = { .pointer = (char*)target, .length = target != NULL ? strlen(target) : 0 },
            .targets = options->targets,
            .target_count = options->target_count,
            .files = files,
            .file_count = file_count,
            .include_system = options->include_system,
            .phony = options->phony,
            .scalar = scalar,
            .omit_source = input_stdin,
        };
        uint64_t length = 0;
        result = path != NULL && target != NULL && dependency_rule(&rule, NULL, 0, &length) == DEPENDENCY_OK;
        char* bytes = result ? arena_push(arena, length, 1) : NULL;
        result = result && bytes != NULL && dependency_rule(&rule, bytes, length, &length) == DEPENDENCY_OK;
        if (result)
        {
            *output = (DependencyOutput){ .path = path, .bytes = { .pointer = bytes, .length = length } };
        }
        else
        {
            fprintf(errors, "error: can't construct dependency rule for '%s': invalid filename or exhausted capacity\n", source);
        }
    }
    return result;
}

// Compare directory identities as well as existing file identities: ./new.o and new.o alias even before either
// exists. A dangling output symlink is conservatively refused because its eventual target cannot be checked yet.
static bool dependency_same_file(const char* a, const char* b)
{
    struct stat first;
    struct stat second;
    const bool have_a = stat(a, &first) == 0;
    const bool have_b = stat(b, &second) == 0;
    bool result = strcmp(a, b) == 0 || (have_a && have_b && first.st_dev == second.st_dev && first.st_ino == second.st_ino);
    if (!result)
    {
        result = (!have_a && lstat(a, &first) == 0 && S_ISLNK(first.st_mode)) || (!have_b && lstat(b, &second) == 0 && S_ISLNK(second.st_mode));
    }
    if (!result)
    {
        const char* slash_a = strrchr(a, '/');
        const char* slash_b = strrchr(b, '/');
        const char* base_a = slash_a != NULL ? slash_a + 1 : a;
        const char* base_b = slash_b != NULL ? slash_b + 1 : b;
        if (strcmp(base_a, base_b) == 0)
        {
            const size_t length_a = slash_a != NULL ? (size_t)(slash_a - a) : 1;
            const size_t length_b = slash_b != NULL ? (size_t)(slash_b - b) : 1;
            char directory_a[PATH_MAX];
            char directory_b[PATH_MAX];
            if (length_a < sizeof(directory_a) && length_b < sizeof(directory_b))
            {
                const size_t bytes_a = length_a != 0 ? length_a : 1;
                const size_t bytes_b = length_b != 0 ? length_b : 1;
                memcpy(directory_a, slash_a != NULL ? a : ".", bytes_a);
                memcpy(directory_b, slash_b != NULL ? b : ".", bytes_b);
                directory_a[bytes_a] = 0;
                directory_b[bytes_b] = 0;
                result = stat(directory_a, &first) == 0 && stat(directory_b, &second) == 0 && first.st_dev == second.st_dev && first.st_ino == second.st_ino;
            }
        }
    }
    return result;
}

// Refuse destructive overlaps before opening primary outputs, including a depfile naming a different input unit.
// This is cold command admission, and runs only for dependency requests. Shared -MF destinations are allowed: the
// argv-last unit replaces the earlier complete rule, as Clang does, without racing concurrent writers.
static bool dependency_check_destinations(const DependencyOptions* options, char** sources, uint32_t count, const char* primary, bool objects, bool executable, Arena* arena)
{
    bool result = true;
    // These option values and source names are immutable during admission. A shared destination still checks
    // every source and artifact, but repeating that same complete check for each unit adds no information.
    const bool enabled = options->enabled;
    const bool only = options->only;
    const bool shared = options->path != NULL || only || primary != NULL;
    const uint32_t destinations = shared && count != 0 ? 1 : count;
    const char** artifacts = NULL;
    for (uint32_t i = 0; enabled && result && i < destinations; i += 1)
    {
        const char* destination = dependency_destination(options, sources[i], primary, arena);
        result = destination != NULL && destination[0] != 0;
        if (result && strcmp(destination, "-") != 0)
        {
            for (uint32_t j = 0; result && j < count; j += 1)
            {
                result = strcmp(sources[j], "-") == 0 || !dependency_same_file(destination, sources[j]);
            }
            // Materialize implicit object names only when a non-stdout destination needs them. The array and
            // strings remain in the command arena, so later destinations reuse the exact same bounded storage.
            if (result && !only && objects && primary == NULL && artifacts == NULL)
            {
                artifacts = arena_push(arena, (uint64_t)count * sizeof(*artifacts), _Alignof(const char*));
                result = artifacts != NULL;
                for (uint32_t j = 0; result && j < count; j += 1)
                {
                    artifacts[j] = dependency_filename(arena, sources[j], NULL, 'o');
                    result = artifacts[j] != NULL;
                }
            }
            for (uint32_t j = 0; result && !only && j < count; j += 1)
            {
                const char* artifact = primary != NULL ? primary : objects ? artifacts[j] : executable ? "a.out" : NULL;
                result = (!objects || artifact != NULL) && (artifact == NULL || !dependency_same_file(destination, artifact));
            }
        }
        if (!result)
        {
            fprintf(stderr, "error: dependency output is empty, unavailable, or aliases an input or primary output\n");
        }
    }
    return result;
}

// Named destinations are replaced only after a complete successful write and close. A temporary sibling makes rename
// atomic and leaves an old depfile intact on failure. Stdout is deliberately not transactional, but its failures are
// reported. The caller, not a unit lane, owns publication. A new depfile gets the mode fopen would give it (0666 and
// the umask); one replacing a regular file keeps that file's permission bits, as clang's truncation does.
// A symlink is not replaced: clang truncates through it, so the target is rewritten in place and the link stays.
// rename would swap the link for a regular file and leave the target stale.
// The selected template must not produce the destination itself: creating that file would expose partial output
// before rename. The two templates have equal lengths and disjoint five-byte prefixes for every random suffix.
static const char* dependency_temporary_template(const char* basename)
{
    return strncmp(basename, ".tmp.", 5) == 0 ? ".tmq.XXXXXX" : ".tmp.XXXXXX";
}

static bool dependency_publish(const DependencyOutput* output, FILE* errors)
{
    bool result = true;
    if (output->path != NULL)
    {
        const char* path = output->path;
        const String8 bytes = output->bytes;
        struct stat linked;
        struct stat existing = { 0 };
        const bool link = lstat(path, &linked) == 0 && S_ISLNK(linked.st_mode);
        const bool found = stat(path, &existing) == 0;
        const bool special = found && !S_ISREG(existing.st_mode);
        if (strcmp(path, "-") == 0)
        {
            result = fwrite(bytes.pointer, 1, (size_t)bytes.length, stdout) == bytes.length && fflush(stdout) == 0;
        }
        else if (link && !special)
        {
            // The target may be absent. fopen follows the link and creates it, which leaves a dangling symlink usable.
            FILE* stream = fopen(path, "wb");
            result = stream != NULL;
            if (stream != NULL)
            {
                const bool written = fwrite(bytes.pointer, 1, (size_t)bytes.length, stream) == bytes.length;
                result = fclose(stream) == 0 && written;
            }
        }
        else if (special)
        {
            // Never rename over a device or FIFO. /dev/null is a common explicit depfile sink; other special
            // destinations are refused rather than blocking on a FIFO or replacing a device node.
            struct stat null_device;
            const bool discard = S_ISCHR(existing.st_mode) && stat("/dev/null", &null_device) == 0 && existing.st_rdev == null_device.st_rdev;
            FILE* stream = discard ? fopen(path, "wb") : NULL;
            result = stream != NULL;
            if (stream != NULL)
            {
                const bool written = fwrite(bytes.pointer, 1, (size_t)bytes.length, stream) == bytes.length;
                result = fclose(stream) == 0 && written;
            }
        }
        else
        {
            // Keep the temporary in the same directory for atomic rename, without extending the destination's
            // basename: a valid destination can already use the filesystem's entire component-length limit.
            const char* slash = strrchr(path, '/');
            const size_t directory = slash != NULL ? (size_t)(slash - path) + 1 : 0;
            const char* pattern = dependency_temporary_template(path + directory);
            char* temporary = directory <= SIZE_MAX - sizeof(".tmp.XXXXXX") ? malloc(directory + sizeof(".tmp.XXXXXX")) : NULL;
            int fd = -1;
            if (temporary != NULL)
            {
                memcpy(temporary, path, directory);
                memcpy(temporary + directory, pattern, sizeof(".tmp.XXXXXX"));
                fd = mkstemp(temporary);
            }
            // mkstemp makes the file private. Give it the destination's permissions before the rename, from the file
            // it replaces or from the umask, so a new depfile is not 0600 and a replaced one keeps its mode.
            bool mode_ready = true;
            if (fd >= 0)
            {
                mode_t mode;
                if (found)
                {
                    mode = existing.st_mode & 0777;
                }
                else
                {
                    const mode_t mask = umask(0);
                    umask(mask);
                    mode = (mode_t)(0666 & ~mask);
                }
                mode_ready = fchmod(fd, mode) == 0;
            }
            FILE* stream = fd >= 0 && mode_ready ? fdopen(fd, "wb") : NULL;
            result = stream != NULL;
            if (stream != NULL)
            {
                const bool written = fwrite(bytes.pointer, 1, (size_t)bytes.length, stream) == bytes.length;
                result = fclose(stream) == 0 && written;
                result = result && rename(temporary, path) == 0;
            }
            else if (fd >= 0)
            {
                close(fd);
            }
            if (!result && fd >= 0)
            {
                unlink(temporary);
            }
            free(temporary);
        }
        if (!result)
        {
            fprintf(errors, "error: can't write dependency output '%s'\n", path);
        }
    }
    return result;
}

// Pipes cannot be mmap'ed. Read until EOF, at most UINT32_MAX bytes, and keep the bytes in `arena`. `quoted` puts
// quotes around `label` in the diagnostic. An empty stream still has a non-null pointer.
static bool driver_read_fd(Arena* arena, int fd, String8* source, const char* label, bool quoted)
{
    const uint64_t mark = arena->length;
    uint64_t length = 0;
    char* first = NULL;
    bool result = true;
    bool end = false;
    while (result && !end)
    {
        const uint64_t remaining = (uint64_t)UINT32_MAX - length;
        const uint64_t chunk = remaining < 65536 ? remaining : 65536;
        if (chunk == 0)
        {
            char extra = 0;
            ssize_t got = 0;
            do
            {
                got = read(fd, &extra, 1);
            }
            while (got < 0 && errno == EINTR);
            result = got == 0;
            end = true;
        }
        else
        {
            char* bytes = arena_push(arena, chunk, 1);
            result = bytes != NULL;
            if (result)
            {
                first = first != NULL ? first : bytes;
                uint64_t filled = 0;
                while (result && filled < chunk && !end)
                {
                    const ssize_t got = read(fd, bytes + filled, (size_t)(chunk - filled));
                    if (got > 0)
                    {
                        filled += (uint64_t)got;
                    }
                    else if (got == 0)
                    {
                        end = true;
                    }
                    else
                    {
                        result = errno == EINTR;
                    }
                }
                length += filled;
                end = end || filled != chunk;
            }
        }
    }
    if (result)
    {
        arena_pop_to(arena, mark + length);
        first = length == 0 ? "" : first;
        *source = (String8){ .pointer = first, .length = length };
    }
    else
    {
        arena_pop_to(arena, mark);
        *source = (String8){ 0 };
        fprintf(stderr, quoted ? "error: can't read '%s': I/O error, source too large, or exhausted capacity\n" :
                                 "error: can't read %s: I/O error, source too large, or exhausted capacity\n", label);
    }
    return result;
}

// Diagnostic replay must not consume stdin twice. Keep a bounded, stable arena copy; the preprocessor copies it
// into its ordinary virtual-file representation, with the name <stdin>.
static bool driver_read_stdin(Arena* arena, String8* source)
{
    return driver_read_fd(arena, STDIN_FILENO, source, "standard input", false);
}

// 1 when `path` is neither a regular file nor a directory and its bytes are in `*source` (a character device, FIFO,
// socket, or /dev/fd pipe). 0 when it is absent, a directory, or a regular file, which the file cache reads.
// -1 when it is a stream and the read fails. Include search does not use this: those probes stay non-blocking.
static int driver_read_nonregular(Arena* arena, const char* path, String8* source)
{
    struct stat status;
    const bool exists = stat(path, &status) == 0;
    const bool stream = exists && !S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode) && !S_ISBLK(status.st_mode);
    int result = 0;
    *source = (String8){ 0 };
    if (stream)
    {
        const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOCTTY);
        const bool read = fd >= 0 && driver_read_fd(arena, fd, source, path, true);
        if (fd >= 0)
        {
            close(fd);
        }
        else
        {
            fprintf(stderr, "error: can't read '%s': %s\n", path, strerror(errno));
        }
        result = read ? 1 : -1;
    }
    return result;
}
