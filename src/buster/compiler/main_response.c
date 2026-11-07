// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-command @file arguments, before information probes and preprocessing/compilation selection (#1476).
// Included only in main.c's driver unit. main_response_expand preserves argument order and the caller's cwd;
// main_response_files walks nested files on an explicit bounded stack. The terminal source command runs once per
// process: its successful argv storage stays rooted here until exit, like the original argv, for any late borrowers.
// Failure releases private storage before dispatch. No shell, environment expansion or exit-handler registration.
#include <stdlib.h>

#define BUSTER_RESPONSE_DEPTH 32
#define BUSTER_RESPONSE_FILES 4096
#define BUSTER_RESPONSE_ARGUMENTS 65536
#define BUSTER_RESPONSE_BYTES (16 * 1024 * 1024)

typedef struct MainResponseText MainResponseText;
struct MainResponseText
{
    MainResponseText* next;
    dev_t device;
    ino_t inode;
    size_t length;
    char bytes[];
};

typedef struct MainResponseFrame MainResponseFrame;
struct MainResponseFrame
{
    MainResponseText* text;
    const char* path;
    size_t cursor;
};

typedef struct MainResponseArguments MainResponseArguments;
struct MainResponseArguments
{
    char** arguments;
    MainResponseText* texts;
    uint32_t count;
    uint32_t files;
    size_t bytes;
};

static MainResponseArguments main_response_arguments;

static void main_response_release(MainResponseArguments* storage)
{
    MainResponseText* text = storage->texts;
    while (text != NULL)
    {
        MainResponseText* next = text->next;
        free(text);
        text = next;
    }
    free(storage->arguments);
    *storage = (MainResponseArguments){ 0 };
}

static bool main_response_space(unsigned char byte)
{
    return byte == ' ' || (byte >= '\t' && byte <= '\r');
}

// Decode one word in place without moving any earlier word. Quotes can join unquoted text, and a backslash quotes
// the next byte inside or outside either kind of quote. Empty quoted words are arguments too.
static bool main_response_word(MainResponseFrame* frame, char** word)
{
    char* bytes = frame->text->bytes;
    const size_t length = frame->text->length;
    size_t read = frame->cursor;
    while (read < length && main_response_space((unsigned char)bytes[read]))
    {
        read += 1;
    }
    *word = read < length ? bytes + read : NULL;
    size_t write = read;
    unsigned char quote = 0;
    bool valid = true;
    while (valid && read < length)
    {
        const unsigned char byte = (unsigned char)bytes[read++];
        if (byte == '\\')
        {
            valid = read < length;
            if (valid)
            {
                bytes[write++] = bytes[read++];
            }
        }
        else if (quote != 0)
        {
            if (byte == quote)
            {
                quote = 0;
            }
            else
            {
                bytes[write++] = (char)byte;
            }
        }
        else if (byte == '\'' || byte == '"')
        {
            quote = byte;
        }
        else if (main_response_space(byte))
        {
            break;
        }
        else
        {
            bytes[write++] = (char)byte;
        }
    }
    bytes[write] = 0;
    frame->cursor = read;
    return valid && quote == 0;
}

// Read a stable regular file only. O_NONBLOCK lets a FIFO be rejected without waiting for a writer. Checking the
// final read catches growth as well as the initial size limit; every occurrence consumes the aggregate budgets.
static MainResponseText* main_response_read(MainResponseArguments* storage, MainResponseFrame* frames, uint32_t depth, const char* path)
{
    MainResponseText* result = NULL;
    const int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    struct stat status;
    bool valid = descriptor >= 0 && fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode) && status.st_size >= 0;
    const char* reason = "cannot read a regular file";
    if (valid && (storage->files == BUSTER_RESPONSE_FILES || (uint64_t)status.st_size > BUSTER_RESPONSE_BYTES - storage->bytes))
    {
        valid = false;
        reason = "expansion limit exceeded";
    }
    for (uint32_t i = 0; valid && i < depth; i += 1)
    {
        if (frames[i].text->device == status.st_dev && frames[i].text->inode == status.st_ino)
        {
            valid = false;
            reason = "nested file cycle";
        }
    }
    if (valid)
    {
        const size_t length = (size_t)status.st_size;
        result = malloc(sizeof(*result) + length + 1);
        valid = result != NULL;
        if (valid)
        {
            *result = (MainResponseText){ .next = storage->texts, .device = status.st_dev, .inode = status.st_ino, .length = length };
            storage->texts = result;
            storage->files += 1;
            storage->bytes += length;
            size_t read_bytes = 0;
            while (valid && read_bytes < length)
            {
                const ssize_t taken = read(descriptor, result->bytes + read_bytes, length - read_bytes);
                if (taken > 0)
                {
                    read_bytes += (size_t)taken;
                }
                else if (taken == 0 || errno != EINTR)
                {
                    valid = false;
                }
            }
            char extra;
            ssize_t taken;
            do
            {
                taken = read(descriptor, &extra, 1);
            }
            while (taken < 0 && errno == EINTR);
            valid = valid && taken == 0 && memchr(result->bytes, 0, length) == NULL;
            result->bytes[length] = 0;
            reason = "read failure, changed size or embedded NUL";
        }
        else
        {
            reason = "allocation failed";
        }
    }
    if (descriptor >= 0)
    {
        valid = close(descriptor) == 0 && valid;
    }
    if (!valid)
    {
        fprintf(stderr, "buster: response file '%s': %s\n", path, reason);
        result = NULL;
    }
    return result;
}

static __attribute__((noinline)) bool main_response_files(int* count, char*** arguments)
{
    MainResponseArguments* storage = &main_response_arguments;
    // This is the terminal command's process-lifetime owner, never a reusable parser or a unit-shared global.
    const bool fresh = storage->arguments == NULL;
    bool valid = fresh;
    if (valid)
    {
        storage->arguments = malloc((BUSTER_RESPONSE_ARGUMENTS + 2) * sizeof(*storage->arguments));
        valid = storage->arguments != NULL;
    }
    if (!valid)
    {
        fputs("buster: response file expansion storage unavailable\n", stderr);
    }
    MainResponseFrame frames[BUSTER_RESPONSE_DEPTH];
    uint32_t depth = 0;
    int original = 1;
    if (valid)
    {
        storage->arguments[storage->count++] = (*arguments)[0];
    }
    while (valid && (depth != 0 || original < *count))
    {
        char* word = NULL;
        if (depth == 0)
        {
            word = (*arguments)[original++];
        }
        else
        {
            MainResponseFrame* frame = &frames[depth - 1];
            valid = main_response_word(frame, &word);
            if (!valid)
            {
                fprintf(stderr, "buster: response file '%s': unterminated quote or escape\n", frame->path);
            }
            if (word == NULL)
            {
                depth -= 1;
            }
        }
        if (valid && word != NULL && word[0] == '@' && word[1] != 0)
        {
            if (depth == BUSTER_RESPONSE_DEPTH)
            {
                fputs("buster: response file nesting limit exceeded\n", stderr);
                valid = false;
            }
            else
            {
                MainResponseText* text = main_response_read(storage, frames, depth, word + 1);
                valid = text != NULL;
                if (valid)
                {
                    frames[depth++] = (MainResponseFrame){ .text = text, .path = word + 1 };
                }
            }
        }
        else if (valid && word != NULL)
        {
            valid = storage->count <= BUSTER_RESPONSE_ARGUMENTS;
            if (valid)
            {
                storage->arguments[storage->count++] = word;
            }
            else
            {
                fputs("buster: response file argument limit exceeded\n", stderr);
            }
        }
    }
    if (valid)
    {
        storage->arguments[storage->count] = NULL;
        *arguments = storage->arguments;
        *count = (int)storage->count;
    }
    else if (fresh)
    {
        main_response_release(storage);
    }
    return valid;
}

// Keep normal compiler starts allocation-free, including their no-@ argv storage. A bare @ remains a literal.
static bool main_response_expand(int* count, char*** arguments)
{
    bool expand = false;
    for (int i = 1; !expand && i < *count; i += 1)
    {
        expand = (*arguments)[i][0] == '@' && (*arguments)[i][1] != 0;
    }
    return !expand || main_response_files(count, arguments);
}
